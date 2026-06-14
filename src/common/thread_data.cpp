#pragma once

#include <cstdint>
#include <iostream>
#include <liburing.h>
#include <limits>

#include "defs.cpp"
#include "async_thread_shared.cpp"
#include "io_event.cpp"

using namespace std;

// Badly designed class; we should record within the event class
// Causes event implementation methods to be defined in this file
// polluting it
class ThreadData {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };

    int thread;

    io_uring ring;
    io_uring_cqe *cqe[128] = {nullptr};

    typedef unordered_map<uint64_t, CallDataThreaded> CallMap;
    typedef unordered_map<uint64_t, ObjectDataThreaded> ObjectMap;
    typedef unordered_map<uint64_t, TimerData> TimerMap;

    CallMap proc_to_dat;
    ObjectMap obj_info;
    TimerMap timers;
    map<uint64_t, list<pair<uint64_t, uint64_t>>> also_notify;

    uint64_t proc_id_base, proc_id_curr; // Upper bound of block is id_base + 1000
    uint64_t obj_id_base, obj_id_curr;
    uint64_t tim_id_base, tim_id_curr;
    uint64_t source_object, source_proc;
    bool o_init = false, p_init = false, t_init = false;

    // thread, object, procedure
    list<tuple<int, uint64_t, uint64_t, FunctionCall::Type>> procedure_calls;
    list<tuple<int, uint64_t>> deleted_ids;
    list<tuple<int, uint64_t, ConstructorCall::Type, shared_ptr<any>>> new_objs;
    list<tuple<int, function<void(io_uring_sqe*)>>> pending_uring;
    list<tuple<uint64_t, __kernel_timespec>> pending_timers;

    vector<PerThread>& per_thread_data;
    IdBlocks& blocks;

    class Context {
        ThreadData& data;
        bool cancel = false;

    public:
        Context(ThreadData& data, uint64_t object_id, uint64_t proc_id) : data(data)
        {
            data.source_object = object_id;
            data.source_proc = proc_id;
            cancel = false;
        }

        void set_cancel(bool val) {
            cancel = val;
        }

        ~Context() {
            for (auto [thread, obj, f, ptr] : data.new_objs) {
                data.obj_info[data.source_object].children[obj] = {};

                ConstructorCall callinfo {
                    .constructor = f,
                    .ci = CallerInfo {
                        .thread_id = data.thread,
                        .obj_id = data.source_object,
                        .proc_id = data.source_proc
                    },
                    .ti = TargetInfo {
                        .obj_id = obj,
                        .proc_id = numeric_limits<uint64_t>::max()
                    },
                    .uring_data = ptr
                };

                data.per_thread_data[thread].q.push(callinfo);
            }

            if (!cancel) {
                for (auto [thread, obj, proc, f] : data.procedure_calls) {
                    auto& info = data.obj_info[data.source_object].children[obj];
                    info.thread = thread;
                    info.procedures.emplace(proc);

                    FunctionCall callinfo = {
                        .call = f,
                        .ci = CallerInfo {
                            .thread_id = data.thread,
                            .obj_id = data.source_object,
                            .proc_id = data.source_proc
                        },
                        .ti = TargetInfo {
                            .obj_id = obj,
                            .proc_id = proc
                        }
                    };

                    data.per_thread_data[thread].q.push(callinfo);
                }

                for (auto& [op, f] : data.pending_uring) {
                    io_uring_sqe* sqe = io_uring_get_sqe(&data.ring);
                    if (sqe) {
                        f(sqe);
                        IoUringAttached* attached = new IoUringAttached;
                        attached->data.emplace<EventData>(EventData{op, data.source_object, data.source_proc});
                        io_uring_sqe_set_data(sqe, attached);
                    }
                }

                for (auto [id, ts] : data.pending_timers) {
                    if (data.timers.contains(id)) {
                        bool zeroed = ts.tv_nsec == 0 && ts.tv_sec == 0;
                        io_uring_sqe* sqe = io_uring_get_sqe(&data.ring);
                        IoUringAttached* attached = new IoUringAttached;
                        io_uring_sqe_set_data(sqe, attached);
                        if (zeroed) {
                            attached->data.emplace<TimerUpdate>(
                                TimerUpdate {
                                    .timer_id = id,
                                    .remove = true
                                }
                            );
                            io_uring_prep_timeout_remove(sqe, (__u64)data.timers[id].ptr, 0);
                        } else {
                            *data.timers[id].ts = ts;
                            attached->data.emplace<TimerUpdate>(
                                TimerUpdate {
                                    .timer_id = id,
                                    .remove = false
                                }
                            );
                            io_uring_prep_timeout_update(sqe, data.timers[id].ts.get(), (__u64)data.timers[id].ptr, 0);
                        }
                    } else {
                        io_uring_sqe* sqe = io_uring_get_sqe(&data.ring);
                        IoUringAttached* attached = new IoUringAttached;
                        data.timers[id] = {
                            .ptr = attached,
                            .obj_id = data.source_object,
                            .ts = make_unique<__kernel_timespec>(ts)
                        };
                        attached->data.emplace<Timer>(
                            Timer {
                                .timer_id = id,
                                .obj_id = data.source_object,
                                .proc_id = data.source_proc
                            }
                        );

                        io_uring_prep_timeout(sqe, data.timers[id].ts.get(), 0, 0);
                        io_uring_sqe_set_data(sqe, attached);
                    }
                }
            }

            for (auto [tid, deleted_id] : data.deleted_ids) {
                data.per_thread_data[tid].q.push(
                    Delete {
                        .ci = CallerInfo {
                            .thread_id = data.thread,
                            .obj_id = data.source_object,
                            .proc_id = data.source_proc
                        },
                        .ti = TargetInfo {
                            .obj_id = deleted_id,
                            .proc_id = numeric_limits<uint64_t>::max()
                        }
                    }
                );
                data.obj_info[data.source_object].children.erase(deleted_id);
            }

            data.procedure_calls.clear();
            data.pending_uring.clear();
            data.pending_timers.clear();
            data.deleted_ids.clear();
            data.new_objs.clear();
        }
    };

public:
    ThreadData(vector<PerThread>& data, IdBlocks& blocks, int thread) :
        thread(thread),
        per_thread_data(data),
        blocks(blocks)
    {
        if (io_uring_queue_init(512, &ring, 0) < 0) {
            throw runtime_error{"Io_Uring ring init failed"};
        }
    }

    void function_call(CallMap& proc_to_dat, ObjectMap& obj_info, FunctionCall& func, int tid);

    Context begin_recording(uint64_t obj_id, uint64_t proc_id = numeric_limits<uint64_t>::max()) {
        return Context(*this, obj_id, proc_id);
    }

    void remove_child(uint64_t object_id, int target_tid) {
        deleted_ids.emplace_back(target_tid, object_id);
    }

    void run() {
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
                surplus_wait_usec = 500;
            }

            int res = io_uring_wait_cqes_min_timeout(&ring, cqe, 128, &ts, surplus_wait_usec, nullptr);
            if (res != -ETIME && res < 0) {
                continue;
            }

            process_uring();

            for (int i = 0; i < 10 && (handle_dequeue() || (q_nonempty = false)); i++);
        }
    }

    /**
     * @returns Thread ID of object, object id
     */
    tuple<int, uint64_t> init(ConstructorCall::Type f) {
        auto id = get_obj_id();

        // Attempt to stay on the same thread, with a lower threshold
        // due to the anticipation of having multiple function calls.
        {
            lock_guard lu(per_thread_data[thread].stats);
            if (per_thread_data[thread].load_avg < 0.8) {
                new_objs.emplace_back(thread, id, f, shared_ptr<any>());
                return {thread, id};
            }
        }

        int min_thread, min_thread_0 = -1; float min_amt = 1.0;

        // Need constant size for iteration
        // lock_guard vec(instance.size_dec);
        for (size_t i = 0; i < per_thread_data.size(); i++) {
            lock_guard t(per_thread_data[i].stats);

            // Don't push to empty thread unless all working threads have too much utilization
            if (per_thread_data[i].load_avg < min_amt) {
                if (per_thread_data[i].load_avg > 0.0) {
                    min_amt = per_thread_data[i].load_avg;
                    min_thread = i;
                } else {
                    min_thread_0 = i;
                }
            }
        }

        if (min_amt > 0.2 && min_thread_0 != -1) {
            new_objs.emplace_back(min_thread_0, id, f, shared_ptr<any>());
            return {min_thread_0, id};
        } else {
            new_objs.emplace_back(min_thread, id, f, shared_ptr<any>());
            return {min_thread, id};
        }
    }

    void call_uring(int op, function<void(io_uring_sqe*)> f) {
        pending_uring.emplace_back(op, f);
    }

    uint64_t call(unordered_set<int> target_tid, uint64_t target_object, FunctionCall::Type f) {
        // Locally get the id by preallocation of a block per thread
        auto id = get_proc_id();

        // Usually child events will be on the same thread, so this will minimize thrashing
        // If a child event is on multiple threads, select the one with the lowest load average
        // TODO Auto-scaling child events if they allow multithreading
        if (target_tid.contains(thread)) {
            // Try to keep ourselves on the core-local cache line
            lock_guard lu(per_thread_data[thread].stats);
            if (per_thread_data[thread].load_avg < 0.9) {
                procedure_calls.emplace_back(thread, target_object, id, f);
            }
        } else {
            int min_thread; float min_amt = 1.0;
            for (auto tid : target_tid) {
                lock_guard lu(per_thread_data[tid].stats);
                if (per_thread_data[tid].load_avg < min_amt) {
                    min_amt = per_thread_data[tid].load_avg;
                    min_thread = tid;
                }
            }
            procedure_calls.emplace_back(min_thread, target_object, id, f);
        }

        return id;
    }

    void del(unordered_set<int> target_tid, uint64_t obj_id) {
        for (auto tid : target_tid) {
            deleted_ids.emplace_back(tid, obj_id);
        }
    }

    uint64_t timer(__kernel_timespec ts) {
        auto id = get_tim_id();

        pending_timers.emplace_back(id, ts);

        return id;
    }

    void cancel_timer(uint64_t timerid) {
        if (!timers.contains(timerid)) return;
        pending_timers.emplace_back(timerid, __kernel_timespec{0, 0});
    }

    void attach(uint64_t id) {
        also_notify[id].emplace_back(source_object, source_proc);
    }

    uint64_t get_proc_id() {
        if (!p_init || proc_id_curr == proc_id_base + 1000) {
            proc_id_base = proc_id_curr = 1000 * blocks.proc_block_++;
            p_init = true;
        }
        return proc_id_curr++;
    }

    uint64_t get_obj_id() {
        if (!o_init || obj_id_curr == obj_id_base + 1000) {
            obj_id_base = obj_id_curr = 1000 * blocks.obj_block_++;
            o_init = true;
        }
        return obj_id_curr++;
    }

    uint64_t get_tim_id() {
        if (!t_init || tim_id_curr == tim_id_base + 1000) {
            tim_id_base = tim_id_curr = 1000 * blocks.timer_block_++;
            t_init = true;
        }
        return tim_id_curr++;
    }

private:
    /*
     * @brief Processes the queue
     * @returns Whether any message was processed at all
     */
    bool handle_dequeue() {
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
            [&] (ProcedureUpdate& upd) {
                obj_info[upd.ti.obj_id].ptr->local_data_.thread_data = this;

                obj_info[upd.ti.obj_id].ptr->start_response(upd.ci.proc_id, upd.resp);
            },
            [&, this] (Delete& del) mutable {
                obj_info[del.ti.obj_id].ptr->local_data_.thread_data = this;

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
                    .uring_data = shared_ptr<any>()
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
            [&] (ConstructResponse& cs) {
                obj_info[cs.target].ptr->resolve(cs.constructed, cs.ev);
            },
            [] (monostate) {

            }
        }, result);

        io_uring_submit(&ring);
        return true;
    }

    void process_uring() {
        int i = 0;
        for (auto ptr = cqe; *ptr && ptr - cqe < 128; ptr++, i++) {
            bool no_delete = false;

            IoUringAttached* uringdata = reinterpret_cast<IoUringAttached*>(io_uring_cqe_get_data(*ptr));
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
                            .res = (*ptr)->res
                        },
                    };

                    per_thread_data[thread].q.push(dt);
                },
                [&] (Timer& tim) {
                    if ((*ptr)->res == -ETIME) {
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
                    } else if ((*ptr)->res != -ECANCELED) {
                        cout << "Unknown timer result: " << (*ptr)->res << endl;
                    }
                },
                [&] (TimerUpdate& tim_upd) {
                    if ((*ptr)->res >= 0 && tim_upd.remove)
                        timers.erase(tim_upd.timer_id);
                }
            }, uringdata->data);

            if (!no_delete)
                delete uringdata;
        }

        io_uring_cq_advance(&ring, i);
    }

    void construct_call(ConstructorCall& construct) {
        // We split construction into two phases
        // The actual constructor is local-only
        // A secondary function construct_with_global() is allowed to
        // make calls, etc

        auto ctx = begin_recording(construct.ti.obj_id);
        obj_info.emplace(construct.ti.obj_id, ObjectDataThreaded {
            .ptr = construct.constructor(),
            .assoc_procs = {},
            .parents = {{construct.ci.thread_id, construct.ci.obj_id}},
            .children = {}
        });

        if (construct.uring_data) {
            obj_info[construct.ti.obj_id].ptr->uring_data_ = reinterpret_pointer_cast<Event::IoUringData>(construct.uring_data);
        } else {
            obj_info[construct.ti.obj_id].ptr->uring_data_ = make_shared<Event::IoUringData>();
        }
        obj_info[construct.ti.obj_id].ptr->local_data_.thread_data = this;
        obj_info[construct.ti.obj_id].ptr->construct_with_global();

        if (construct.ci.thread_id >= 0
            && construct.ci.obj_id != numeric_limits<uint64_t>::max()) {
            auto resp = ConstructResponse {
                .target = construct.ci.obj_id,
                .constructed = construct.ti.obj_id,
                .ev = obj_info[construct.ti.obj_id].ptr
            };
            per_thread_data[construct.ci.thread_id].q.push(resp);
        }
    }

    void function_call(FunctionCall& func) {
        obj_info[func.ti.obj_id].ptr->local_data_.thread_data = this;
        auto ctx = begin_recording(func.ti.obj_id, func.ti.proc_id);

        proc_to_dat[func.ti.proc_id].assoc_obj = func.ti.obj_id;
        proc_to_dat[func.ti.proc_id].back_notify = {func.ci.thread_id, func.ci.obj_id, func.ci.proc_id};
        proc_to_dat[func.ti.proc_id].status = CallStatus::Running;

        CallResponse resp = func.call(obj_info[func.ti.obj_id].ptr, func.ti.proc_id);

        if (static_cast<uint64_t>(func.ci.thread_id) < per_thread_data.size()) {
            per_thread_data[func.ci.thread_id].q.push(
                ProcedureUpdate {
                    .type = PUType::StartConfirm,
                    .resp = resp,
                    .ci = CallerInfo {
                        .thread_id = thread,
                        .obj_id = func.ti.obj_id,
                        .proc_id = func.ti.proc_id
                    },
                    .ti = TargetInfo {
                        .obj_id = func.ci.obj_id,
                        .proc_id = func.ci.proc_id
                    }
                }
            );
        }

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

    void data_received(Data& on_data) {
        if (!obj_info.contains(on_data.ti.obj_id)
            || !proc_to_dat.contains(on_data.ti.proc_id)) {
            return;
        }

        obj_info[on_data.ti.obj_id].ptr->local_data_.thread_data = this;

        auto ctx = begin_recording(
            on_data.ti.obj_id, on_data.ti.proc_id);

        auto res = obj_info[on_data.ti.obj_id].ptr->on_yield(on_data.data);
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
};

template <typename Obj, typename... Args>
size_t Event::i(Args... args) {
    auto f = [args...] mutable {
        return make_shared<Obj>(std::forward<Args>(args)...);
    };

    auto [thread, id] = local_data_.thread_data->init(f);
    auto& vec_evs = uring_data_->sub_events[type_index(typeid(Obj))];

    uring_data_->awaiting_resolve.emplace(id, type_index(typeid(Obj)));
    auto it = find(vec_evs.begin(), vec_evs.end(), nullopt);
    if (it == vec_evs.end()) {
        vec_evs.emplace_back(EventInfo {id, {thread}, weak_ptr<Event>()});
        return vec_evs.size() - 1;
    } else {
        it->emplace(EventInfo {id, {thread}, weak_ptr<Event>()});
        return it - vec_evs.begin();
    }
}

template <typename Obj, typename... Args, typename... FnArgs>
uint64_t Event::c(size_t idx, CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args) {
    auto f = [args..., fun] (shared_ptr<Event> obj, uint64_t id) mutable -> CallResponse {
        return (static_pointer_cast<Obj>(obj).get()->*fun)(id, std::forward<Args>(args)...);
    };

    EventInfo& info = uring_data_->sub_events.at(type_index(typeid(Obj))).at(idx).value();
    return local_data_.thread_data->call(info.thread_id, info.object_id, f);
}

template <typename Obj, typename... Args, typename... FnArgs>
uint64_t Event::c(CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args) {
    auto& evs_ty = uring_data_->sub_events.at(type_index(typeid(Obj)));
    auto it = find_if_not(evs_ty.begin(), evs_ty.end(), [](auto& ev) {return ev == nullopt;});
    if (it == evs_ty.end())
        throw runtime_error{"No event for call found."};

    return c(it - evs_ty.begin(), fun, args...);
}

template <typename... Args, typename... FnArgs>
void Event::c(int op, void(*liburing)(io_uring_sqe*, Args...), FnArgs... args) {
    auto f = [args..., liburing](io_uring_sqe* sqe) mutable {
        liburing(sqe, args...);
    };

    local_data_.thread_data->call_uring(op, f);
}

template <typename Obj>
void Event::d(size_t idx) {
    auto opt = uring_data_->sub_events.at(type_index(typeid(Obj))).at(idx);
    EventInfo& ev = opt.value();

    local_data_.thread_data->del(ev.thread_id, ev.object_id);
    uring_data_->sub_events[type_index(typeid(Obj))][idx] = nullopt;
}

void Event::attach(uint64_t id) {
    local_data_.thread_data->attach(id);
}

uint64_t Event::timer(__kernel_timespec ts) {
    return local_data_.thread_data->timer(ts);
}

void Event::cancel_timer(uint64_t timerid) {
    local_data_.thread_data->cancel_timer(timerid);
}

void Event::resolve(uint64_t id, shared_ptr<Event> ptr) {
    type_index idx = uring_data_->awaiting_resolve.at(id);
    for (auto& evinfo : uring_data_->sub_events[idx]) {
        if (evinfo && evinfo.value().object_id == id) {
            evinfo.value().event = ptr;
        }
    }
}
