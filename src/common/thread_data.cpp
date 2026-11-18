#include "thread_data.h"

#include <cstdint>
#include <iostream>
#include <liburing.h>
#include <limits>
#include <memory>
#include <malloc.h>

#include "defs.h"
#include "async_thread_shared.h"
#include "event.h"
#include "alloc_wrapper.h"

using namespace std;
class ThreadData;

thread_local shared_ptr<IoUringData> context;
thread_local AllocatorWrapper alloc;
thread_local uint64_t uring_id = 0;
thread_local ThreadData* tdata;

ThreadData::ThreadData(vector<PerThread>& data, IdBlocks& blocks, int thread) :
    thread(thread),
    per_thread_data(data),
    blocks(blocks)
{
    if (io_uring_queue_init(512, &ring, 0) < 0) {
        throw runtime_error{"Io_Uring ring init failed"};
    }
}

void ThreadData::run() {
    tdata = this;
    __kernel_timespec ts ={
        .tv_sec = 0,
        .tv_nsec = 100000
    };

    bool q_nonempty = true;
    int surplus_wait_usec = 0;

    while (true) {
        if (q_nonempty) {
            // Fastpath: Behaves like io_uring_wait_cqe_timeout
            ts.tv_nsec = 100000;
            ts.tv_sec = 0;
            surplus_wait_usec = 0;
        } else {
            // Can allow some time for batch to pile up (0.5ms), and can long-wait
            q_nonempty = true; // Reset value
            ts.tv_nsec = 0;
            ts.tv_sec = 10;
            surplus_wait_usec = 100;
        }

        int res = io_uring_wait_cqes_min_timeout(&ring, &cqe, 128, &ts, surplus_wait_usec, nullptr);

        if (res != -ETIME && res < 0) {
            continue;
        }

        process_uring();

        for (int i = 0; i < 256 && (handle_dequeue() || (q_nonempty = false)); i++);

        queue_uring();
        io_uring_submit(&ring);
    }
}

uint64_t ThreadData::get_proc_id() {
    if (!p_init || proc_id_curr == proc_id_base + 1000) {
        proc_id_base = proc_id_curr = 1000 * blocks.proc_block_++;
        p_init = true;
    }
    return proc_id_curr++;
}

uint64_t ThreadData::get_obj_id() {
    if (!o_init || obj_id_curr == obj_id_base + 1000) {
        obj_id_base = obj_id_curr = 1000 * blocks.obj_block_++;
        o_init = true;
    }
    return obj_id_curr++;
}

uint64_t ThreadData::get_tim_id() {
    if (!t_init || tim_id_curr == tim_id_base + 1000) {
        tim_id_base = tim_id_curr = 1000 * blocks.timer_block_++;
        t_init = true;
    }
    return tim_id_curr++;
}

uint64_t ThreadData::co_register(function<void(io_uring_sqe*)> sfunc, coroutine_handle<> handle) {
    uint64_t id = get_proc_id();
    sqes.emplace_back(sfunc, id);
    readiness_waiters[id].handle = handle;
    return id;
}

void ThreadData::co_deregister(uint64_t id) {
    readiness_waiters.erase(id);
}

int ThreadData::co_result(uint64_t id) {
    return readiness_waiters[id].result;
}

bool ThreadData::handle_dequeue() {
    PerThread::EventVariant result;
    bool success = per_thread_data[thread].q.try_pop(result);
    if (!success) {
        return false;
    };

    visit(overloaded {
        [&] (Root& root) {
            auto ptr = root.constructor();
            auto ptask = root.fn(ptr);
            root_tasks.emplace_back(ptask, ptr);
            ptask->resume();
        },
        [&] (CqeReady& cqe_notif) {
            if (!(readiness_waiters.contains(cqe_notif.id) && readiness_waiters[cqe_notif.id].handle)) return;

            readiness_waiters[cqe_notif.id].result = cqe_notif.result;

            // Handle should still be valid atp
            readiness_waiters[cqe_notif.id].handle.resume();

            // TODO activate barrier'd coroutines. Do we need to do this?
        },
        [] (monostate) {

        }
    }, result);

    return true;
}

void ThreadData::queue_uring() {
    size_t sqes_len = sqes.size();
    for (size_t i = 0; i < 512 && i < sqes_len; i++) {
        auto& [call, id] = sqes.front();

        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if (sqe) {
            call(sqe);
            IoUringAttached* attached = new IoUringAttached;
            attached->data.emplace<CoEvent>(CoEvent{.id = id});
            io_uring_sqe_set_data(sqe, attached);
        }
        sqes.pop_front();
    }
}

void ThreadData::process_uring() {
    int i = 0;
    unsigned head;
    io_uring_cqe* ptr;
    io_uring_for_each_cqe(&ring, head, ptr) {
        bool no_delete = false;

        IoUringAttached* uringdata = reinterpret_cast<IoUringAttached*>(io_uring_cqe_get_data(ptr));
        visit(overloaded {
            // [&] (Timer& tim) {
            //     if (ptr->res == -ETIME) {
            //         Data data = {
            //             .ci = {
            //                 .thread_id = -1,
            //                 .obj_id = numeric_limits<uint64_t>::max(),
            //                 .proc_id = numeric_limits<uint64_t>::max()
            //             },
            //             .ti = {
            //                 .obj_id = tim.obj_id,
            //                 .proc_id = tim.proc_id
            //             },
            //             .data = Timeout {
            //                 .timer_id = tim.timer_id
            //             }
            //         };
            //
            //         per_thread_data[thread].q.push(data);
            //     } else if (ptr->res != -ECANCELED) {
            //         cout << "Unknown timer result: " << ptr->res << endl;
            //     }
            // },
            // [&] (TimerUpdate& tim_upd) {
            //     if (ptr->res >= 0 && tim_upd.remove)
            //         timers.erase(tim_upd.timer_id);
            // },
            [&] (CoEvent& event) {
                per_thread_data[thread].q.push(CqeReady {
                    .id = event.id,
                    .result = ptr->res
                });
            }
        }, uringdata->data);

        if (!no_delete)
            delete uringdata;

        i++;
    }

    io_uring_cq_advance(&ring, i);
}


int ThreadData::find_target_thread(unordered_set<int> threads) {
        // Attempt to stay on the same thread, with a lower threshold
    // due to the anticipation of having multiple function calls.
    if (threads.empty() || threads.contains(thread)){
        lock_guard lu(per_thread_data[thread].stats);
        if (per_thread_data[thread].load_avg < 0.8) {
            return thread;
        }
    }

    int min_thread, min_thread_0 = -1, run_thread = -1; float min_amt = 1.0;

    // Need constant size for iteration
    // lock_guard vec(instance.size_dec);
    for (size_t i = 0; i < per_thread_data.size(); i++) {
        if (!threads.empty() && !threads.contains(i)) continue;
        lock_guard t(per_thread_data[i].stats);

        // Don't push to empty thread unless all working threads have too much utilization
        if (per_thread_data[i].load_avg < min_amt) {
            if (per_thread_data[i].load_avg > 0.02) {
                min_amt = per_thread_data[i].load_avg;
                min_thread = i;
            } else {
                min_thread_0 = i;
            }
        }
    }

    if (min_amt > 0.2 && min_thread_0 != -1) {
        run_thread = min_thread_0;
    } else {
        run_thread = min_thread;
    }

    return run_thread;
}
