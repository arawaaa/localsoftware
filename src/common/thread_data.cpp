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

pair<uint64_t, list<Waiters::awaits>::iterator> ThreadData::add_co_uring(function<void(io_uring_sqe*)> sfunc, int* res) {
    uint64_t id = get_proc_id();
    auto it = readiness_waiters[id].waiters.emplace(readiness_waiters[id].waiters.end(), Waiters::awaits {res, {}});
    return {id, it};
}

bool ThreadData::handle_dequeue() {
    PerThread::EventVariant result;
    bool success = per_thread_data[thread].q.try_pop(result);
    if (!success) {
        return false;
    };

    visit(overloaded {
        [&] (ConstructorCall& construct) {
            construct_call(construct);
        },
        [&, this] (FunctionCall& func) {
            function_call(func);
        },
        [&, this] (Delete& del) mutable {
            auto& children = obj_info[del.ti.obj_id].children;
            // Propagate deletions
            for (auto [objid, info] : children) {
                per_thread_data[info.thread].q.push({
                    Delete {
                        // Forward call on behalf of caller
                        .ci = del.ci,
                        .ti = TargetInfo {
                            .obj_id = objid,
                            .proc_id = numeric_limits<uint64_t>::max()
                        }
                    }
                });
            }
            for (auto proc_id : obj_info[del.ti.obj_id].assoc_procs) {
                proc_to_dat.erase(proc_id);
            }

            obj_info.erase(del.ti.obj_id);
        },
        [&] (Data& on_data) {
            data_received(on_data);
        },
        [&](RootStart& rs) {
            // Dissemble into Constructor and FunctionCall, do nothing else
            ConstructorCall cons = {
                .constructor = rs.constructor,
                .ci = CallerInfo {
                    .thread_id = -1,
                    .obj_id = numeric_limits<uint64_t>::max(),
                    .proc_id = numeric_limits<uint64_t>::max()
                },
                .ti = TargetInfo {
                    .obj_id = rs.ti.obj_id,
                    .proc_id = numeric_limits<uint64_t>::max()
                },
                .uring_data = shared_ptr<IoUringData>()
            };

            FunctionCall fun = {
                .call = rs.init,
                .ci = CallerInfo {
                    .thread_id = -1,
                    .obj_id = numeric_limits<uint64_t>::max(),
                    .proc_id = numeric_limits<uint64_t>::max()
                },
                .ti = rs.ti
            };

            per_thread_data[thread].q.push(cons);
            per_thread_data[thread].q.push(fun);
        },
        [] (monostate) {

        }
    }, result);

    io_uring_submit(&ring);
    return true;
}

void ThreadData::process_uring() {
    int i = 0;
    unsigned head;
    io_uring_cqe* ptr;
    io_uring_for_each_cqe(&ring, head, ptr) {
        bool no_delete = false;

        IoUringAttached* uringdata = reinterpret_cast<IoUringAttached*>(io_uring_cqe_get_data(ptr));
        visit(overloaded {
            [&] (EventData& ev) {
                // All class operations occur on the same thread, so this is safe
                Data dt {
                    .ci = CallerInfo {
                        .thread_id = -1,
                        .obj_id = numeric_limits<uint64_t>::max(), // Nonexistent originator
                        .proc_id = numeric_limits<uint64_t>::max()
                    },
                    .ti = TargetInfo {
                        .obj_id = ev.obj_id,
                        .proc_id = ev.proc_id
                    },
                    .data = IoUringResult {
                        .calling_id = ev.proc_id,
                        .op = ev.op,
                        .res = ptr->res
                    },
                };

                per_thread_data[thread].q.push(dt);
            },
            [&] (Timer& tim) {
                if (ptr->res == -ETIME) {
                    Data data = {
                        .ci = {
                            .thread_id = -1,
                            .obj_id = numeric_limits<uint64_t>::max(),
                            .proc_id = numeric_limits<uint64_t>::max()
                        },
                        .ti = {
                            .obj_id = tim.obj_id,
                            .proc_id = tim.proc_id
                        },
                        .data = Timeout {
                            .timer_id = tim.timer_id
                        }
                    };

                    per_thread_data[thread].q.push(data);
                } else if (ptr->res != -ECANCELED) {
                    cout << "Unknown timer result: " << ptr->res << endl;
                }
            },
            [&] (TimerUpdate& tim_upd) {
                if (ptr->res >= 0 && tim_upd.remove)
                    timers.erase(tim_upd.timer_id);
            }
        }, uringdata->data);

        if (!no_delete)
            delete uringdata;

        i++;
    }

    io_uring_cq_advance(&ring, i);
}

void ThreadData::construct_call(ConstructorCall& construct) {
    context = reinterpret_pointer_cast<IoUringData>(std::move(construct.uring_data));
    obj_info.emplace(construct.ti.obj_id, ObjectDataThreaded {
        .ptr = construct.constructor(),
        .assoc_procs = {},
        .parents = {{construct.ci.thread_id, construct.ci.obj_id}},
        .children = {}
    });

    process_queued(obj_info[construct.ti.obj_id].ptr, construct.ti);
}

void ThreadData::function_call(FunctionCall& func) {
    obj_info[func.ti.obj_id].assoc_procs.push_back(func.ti.proc_id);

    proc_to_dat[func.ti.proc_id].assoc_obj = func.ti.obj_id;
    proc_to_dat[func.ti.proc_id].back_notify = {func.ci.thread_id, func.ci.obj_id, func.ci.proc_id};
    proc_to_dat[func.ti.proc_id].status = CallStatus::Running;

    CallResponse resp = func.call(obj_info[func.ti.obj_id].ptr, func.ti.proc_id);

    process_queued(obj_info[func.ti.obj_id].ptr, func.ti);

    proc_to_dat[func.ti.proc_id].op_hint = resp.op_hint;
    proc_to_dat[func.ti.proc_id].description = resp.description;
    proc_to_dat[func.ti.proc_id].status = resp.success ? CallStatus::Running : CallStatus::Degraded;

    if (resp.ret) {
        auto [failed, code] = resp.ret.value();
        proc_to_dat[func.ti.proc_id].status = failed ? CallStatus::Failed : CallStatus::Finished;
        proc_to_dat[func.ti.proc_id].return_code = code;

        if (static_cast<uint64_t>(func.ci.thread_id) >= per_thread_data.size()) return;

        // We don't iterate through back_notify since at this point only the notifiable entity is the
        // calling procedure at thread_id.
        per_thread_data[func.ci.thread_id].q.push(
            Data {
                .ci = CallerInfo {
                    .thread_id = thread,
                    .obj_id = func.ti.obj_id,
                    .proc_id = func.ti.proc_id
                },
                .ti = TargetInfo {
                    .obj_id = func.ci.obj_id,
                    .proc_id = func.ci.proc_id
                },
                .data = ChildTaskCompletion {
                    .calling_id = func.ci.proc_id,
                    .task_id = func.ti.proc_id,
                    .status = proc_to_dat[func.ti.proc_id].status,
                    .return_code = code
                }
            }
        );
    }
}

void ThreadData::data_received(Data& on_data) {
    if (!obj_info.contains(on_data.ti.obj_id)
        || !proc_to_dat.contains(on_data.ti.proc_id)) {
        return;
    }

    obj_info[on_data.ti.obj_id].ptr->map_event_data(on_data.data);
    auto res = obj_info[on_data.ti.obj_id].ptr->on_yield(on_data.data);
    process_queued(obj_info[on_data.ti.obj_id].ptr, on_data.ti);
    if (res) {
        pair<bool, int> yielded = res.value();

        proc_to_dat[on_data.ti.proc_id].status = yielded.first ? CallStatus::Failed : CallStatus::Finished;

        Data completed_notification = {
            .ci = CallerInfo {
                .thread_id = thread,
                .obj_id = on_data.ti.obj_id,
                .proc_id = on_data.ti.proc_id
            },
            .ti = TargetInfo {
                .obj_id = get<1>(proc_to_dat[on_data.ti.proc_id].back_notify),
                .proc_id = get<2>(proc_to_dat[on_data.ti.proc_id].back_notify)
            },
            .data = ChildTaskCompletion {
                .calling_id = get<2>(proc_to_dat[on_data.ti.proc_id].back_notify),
                .task_id = on_data.ti.proc_id,
                .status = proc_to_dat[on_data.ti.proc_id].status,
                .return_code = yielded.second
            }
        };

        per_thread_data[get<0>(proc_to_dat[on_data.ti.proc_id].back_notify)].q.push(completed_notification);
    }

    if (also_notify.contains(on_data.ti.proc_id)) {
        for (auto [obj, proc] : also_notify[on_data.ti.proc_id]) {
            Data copy = on_data;
            copy.ti = TargetInfo {
                .obj_id = obj,
                .proc_id = proc
            };
            per_thread_data[thread].q.push(copy);
        }
    }
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

void ThreadData::process_queued(shared_ptr<Event> ptr, TargetInfo ti) {
    process_queued_construct(ptr, ti);
    process_queued_function(ptr, ti);
    process_queued_uring(ptr, ti);
    process_queued_timers(ptr, ti);
    process_queued_delete(ptr, ti);
    process_queued_attach(ptr, ti);
    ptr->clear();
}

void ThreadData::process_queued_construct(shared_ptr<Event> ptr, TargetInfo ti) {
    list<EventQueuedConstruct>& constructs = ptr->get_queued<EventQueuedConstruct>();
    for (auto& construct : constructs) {
        auto& opt_ev = ptr->get_evinfo(construct.idx, construct.vidx);
        if (!opt_ev)
            continue;

        int run_thread = find_target_thread();
        auto new_obj_id = get_obj_id();
        EventInfo& evinfo = opt_ev.value();
        evinfo.locator = EventInfo::Locator {
            .object_id = new_obj_id,
            .thread_id = {run_thread}
        };

        obj_info[ti.obj_id].children[new_obj_id] = {};

        ConstructorCall callinfo {
            .constructor = construct.fun,
            .ci = CallerInfo {
                .thread_id = run_thread,
                .obj_id = ti.obj_id,
                .proc_id = ti.proc_id
            },
            .ti = TargetInfo {
                .obj_id = new_obj_id,
                .proc_id = numeric_limits<uint64_t>::max()
            },
            .uring_data = std::move(construct.uring_data)
        };

        per_thread_data[thread].q.push(callinfo);
    }
}

void ThreadData::process_queued_function(shared_ptr<Event> ptr, TargetInfo ti) {
    auto& funcs = ptr->get_queued<EventQueuedFunction>();

    for (auto& func : funcs) {
        auto& evinfo_opt = ptr->get_evinfo(func.idx, func.vidx);

        if (!evinfo_opt) continue;

        EventInfo& evinfo = evinfo_opt.value();

        if (!evinfo.locator) {
            throw invalid_argument{"Object was not initialized"};
        }

        EventInfo::Locator& locator = evinfo.locator.value();

        int target_thread = find_target_thread(locator.thread_id);

        uint64_t proc_id = get_proc_id();
        auto& info = obj_info[ti.obj_id].children[locator.object_id];
        info.thread = thread;
        info.procedures.emplace(proc_id);

        FunctionCall callinfo = {
            .call = func.fun,
            .ci = CallerInfo {
                .thread_id = thread,
                .obj_id = ti.obj_id,
                .proc_id = ti.proc_id
            },
            .ti = TargetInfo {
                .obj_id = locator.object_id,
                .proc_id = proc_id
            }
        };

        ptr->global_to_local(proc_id, func.local_id);

        per_thread_data[target_thread].q.push(callinfo);
    }
}

void ThreadData::process_queued_uring(shared_ptr<Event> ptr, TargetInfo ti) {
    auto& urings = ptr->get_queued<EventQueuedUring>();

    for (auto& uring : urings) {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if (sqe) {
            uring.fun(sqe);
            IoUringAttached* attached = new IoUringAttached;
            attached->data.emplace<EventData>(EventData{uring.op, ti.obj_id, ti.proc_id});
            io_uring_sqe_set_data(sqe, attached);
        }
    }
}

void ThreadData::process_queued_timers(shared_ptr<Event> ptr, TargetInfo ti) {
    auto& timersl = ptr->get_queued<EventQueuedTimer>();

    for (auto& timer : timersl) {
        if (timers.contains(ptr->translate_tim_local(timer.local_id))) {
            bool zeroed = timer.time.tv_nsec == 0 && timer.time.tv_sec == 0;
            io_uring_sqe* sqe = io_uring_get_sqe(&ring);
            IoUringAttached* attached = new IoUringAttached;
            io_uring_sqe_set_data(sqe, attached);
            if (zeroed) {
                attached->data.emplace<TimerUpdate>(
                    TimerUpdate {
                        .timer_id = ptr->translate_tim_local(timer.local_id),
                        .remove = true
                    }
                );
                io_uring_prep_timeout_remove(sqe, (__u64)timers[ptr->translate_tim_local(timer.local_id)].ptr, 0);
            } else {
                *timers[ptr->translate_tim_local(timer.local_id)].ts = timer.time;
                attached->data.emplace<TimerUpdate>(
                    TimerUpdate {
                        .timer_id = ptr->translate_tim_local(timer.local_id),
                        .remove = false
                    }
                );
                io_uring_prep_timeout_update(sqe, timers[ptr->translate_tim_local(timer.local_id)].ts.get(), (__u64)timers[ptr->translate_tim_local(timer.local_id)].ptr, 0);
            }
        } else {
            uint64_t id = get_tim_id();
            ptr->global_to_local_tim(id, timer.local_id);
            io_uring_sqe* sqe = io_uring_get_sqe(&ring);
            IoUringAttached* attached = new IoUringAttached;
            timers[id] = {
                .ptr = attached,
                .obj_id = ti.obj_id,
                .ts = make_unique<__kernel_timespec>(timer.time)
            };
            attached->data.emplace<Timer>(
                Timer {
                    .timer_id = id,
                    .obj_id = ti.obj_id,
                    .proc_id = ti.proc_id
                }
            );

            io_uring_prep_timeout(sqe, timers[id].ts.get(), 0, 0);
            io_uring_sqe_set_data(sqe, attached);
        }
    }
}

void ThreadData::process_queued_delete(shared_ptr<Event> ptr, TargetInfo ti) {
    auto& deleted = ptr->get_queued<EventQueuedDelete>();
    for (auto& deleted_s : deleted) {
        for (int id : deleted_s.thread) {
            per_thread_data[id].q.push(
                Delete {
                    .ci = CallerInfo {
                        .thread_id = thread,
                        .obj_id = ti.obj_id,
                        .proc_id = ti.proc_id
                    },
                    .ti = TargetInfo {
                        .obj_id = deleted_s.obj_id,
                        .proc_id = numeric_limits<uint64_t>::max()
                    }
                }
            );
        }
        obj_info[ti.obj_id].children.erase(deleted_s.obj_id);
    }
}

void ThreadData::process_queued_attach(shared_ptr<Event> ptr, TargetInfo ti) {
        auto& attaches = ptr->get_queued<EventQueuedAttach>();

        for (auto& attach : attaches) {
            also_notify[ptr->translate_proc_local(attach.target_local_id)].emplace_back(ti.obj_id, ti.proc_id);
    }
}
