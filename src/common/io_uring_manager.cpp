#pragma once

#include <cstdint>
#include <limits>
#include <new>
#include <variant>
#include <vector>
#include <functional>
#include <tuple>
#include <utility>
#include <optional>
#include <typeindex>
#include <queue>
#include <liburing.h>
#include <ranges>
#include <functional>

#include <oneapi/tbb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "io_event.cpp"
#include "defs.cpp"

using namespace std;

class AsyncHandler {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    friend class Event;
    friend class ThreadData;

    static AsyncHandler& self(int ts = 1) {
        static AsyncHandler instance(ts);
        return instance;
    }

    void run(int tid);

    SSL_CTX* get_tls_ctx() {
        return tls_ctx_;
    }

    /**
     * @brief Initializes the event, sends it to a thread
     * The root event class MUST define init(uint64_t)
     */
    template <typename Obj, typename... Args>
    void initialize_root_event(Args&&... args) {
        auto f = [args...] mutable {
            return make_shared<Obj>(std::forward<Args>(args)...);
        };
    }

    typedef unordered_map<uint64_t, CallDataThreaded> CallMap;
    typedef unordered_map<uint64_t, ObjectDataThreaded> ObjectMap;
    typedef unordered_map<uint64_t, TimerData> TimerMap;
private:

    uint64_t get_proc_block() {
        return proc_block_++;
    }

    uint64_t get_obj_block() {
        return obj_block_++;
    }

    uint64_t get_timer_block() {
        return timer_block_++;
    }

    void function_call(CallMap& proc_to_dat, ObjectMap& obj_info, FunctionCall& func, int tid);

    AsyncHandler(int ts): per_thread_data(ts) {
        tls_ctx_ = SSL_CTX_new(TLS_server_method());
        if (tls_ctx_ == NULL) {
            throw runtime_error{"Unable to create libssl context"};
        }

        if (!SSL_CTX_set_min_proto_version(tls_ctx_, TLS1_2_VERSION)) {
            SSL_CTX_free(tls_ctx_);
            throw runtime_error{"Unable to set minimum TLS version to 1.2"};
        }

        auto opts = SSL_OP_IGNORE_UNEXPECTED_EOF | SSL_OP_NO_RENEGOTIATION | SSL_OP_CIPHER_SERVER_PREFERENCE;
        SSL_CTX_set_options(tls_ctx_, opts);

        if (SSL_CTX_use_certificate_chain_file(tls_ctx_, "/etc/letsencrypt/live/arnavrawat.xyz/fullchain.pem") <= 0) {
            SSL_CTX_free(tls_ctx_);
            throw runtime_error{"Unable to load certificate chain"};
        }

        if (SSL_CTX_use_PrivateKey_file(tls_ctx_, "/etc/letsencrypt/live/arnavrawat.xyz/privkey.pem", SSL_FILETYPE_PEM) <= 0) {
            SSL_CTX_free(tls_ctx_);
            throw runtime_error{"Unable to load private key. Check for certificate / key mismatch."};
        }

        // No session resumption for now

        SSL_CTX_set_verify(tls_ctx_, SSL_VERIFY_NONE, NULL);
    };

    ~AsyncHandler() {
        SSL_CTX_free(tls_ctx_);
    }

    SSL_CTX* tls_ctx_;

    atomic<uint64_t> proc_block_ = 0;
    atomic<uint64_t> obj_block_ = 0;
    atomic<uint64_t> timer_block_ = 0;

    int pt_idx = 0;

    struct alignas(hardware_destructive_interference_size) PerThread {
        typedef variant<monostate, ConstructorCall, FunctionCall, ProcedureUpdate, Delete, Data> EventVariant;
        typedef oneapi::tbb::concurrent_bounded_queue<EventVariant> WorkQueue;
        WorkQueue q;

        mutex stats;
        bool idle = true;
        size_t num_ev;
        float load_avg = 0.0;
    };

    /** For operations impacting the size, i.e. auto-scaling, we will
     * halt all threads with a barrier, then will increase the size.
     * Size decreases are handled by the threads alone, and only if they
     * are the last element in the thread. Threads choose to destruct
     * if they are inactive.
     */
    mutex size_dec;
    vector<PerThread> per_thread_data;

    map<type_index, vector<shared_ptr<Event>>> root_events_;
};

class ThreadData {
    int thread;

    io_uring* ring;

    AsyncHandler::CallMap& proc_to_dat;
    AsyncHandler::ObjectMap& obj_info;
    AsyncHandler::TimerMap& timers;

    uint64_t proc_id_base, proc_id_curr; // Upper bound of block is id_base + 1000
    uint64_t obj_id_base, obj_id_curr;
    uint64_t tim_id_base, tim_id_curr;
    uint64_t source_object, source_proc;
    bool o_init = false, p_init = false, t_init = false;

    // thread, object, procedure
    list<tuple<int, uint64_t, uint64_t, FunctionCall::Type>> procedure_calls;
    list<tuple<int, uint64_t>> deleted_ids;
    list<tuple<int, uint64_t, ConstructorCall::Type>> new_objs;
    list<tuple<int, function<void(io_uring_sqe*)>>> pending_uring;
    list<tuple<uint64_t, __kernel_timespec>> pending_timers;

    AsyncHandler& instance;

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
            for (auto [thread, obj, f] : data.new_objs) {
                data.obj_info[data.source_object].children.insert(obj, {});

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
                    }
                };

                data.instance.per_thread_data[thread].q.push(callinfo);
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

                    data.instance.per_thread_data[thread].q.push(callinfo);
                }

                for (auto& [op, f] : data.pending_uring) {
                    io_uring_sqe* sqe = io_uring_get_sqe(data.ring);
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
                        io_uring_sqe* sqe = io_uring_get_sqe(data.ring);
                        IoUringAttached* attached = new IoUringAttached;
                        io_uring_sqe_set_data(sqe, attached);
                        if (zeroed) {
                            attached->data.emplace<TimerUpdate>(TimerUpdate {true});
                            io_uring_prep_timeout_remove(sqe, (__u64)data.timers[id].ptr, 0);
                        } else {
                            attached->data.emplace<TimerUpdate>(TimerUpdate {false});
                            io_uring_prep_timeout_update(sqe, &ts, (__u64)data.timers[id].ptr, 0);
                        }
                    } else {
                        io_uring_sqe* sqe = io_uring_get_sqe(data.ring);
                        IoUringAttached* attached = new IoUringAttached;
                        data.timers[id] = {
                            .ptr = attached,
                            .obj_id = data.source_object,
                            .ts = make_unique<__kernel_timespec>(ts)
                        };
                        attached->data.emplace<Timer>(Timer {id});

                        io_uring_prep_timeout(sqe, data.timers[id].ts.get(), 0, 0);
                        io_uring_sqe_set_data(sqe, attached);
                    }
                }
            }

            for (auto [tid, deleted_id] : data.deleted_ids) {
                data.instance.per_thread_data[tid].q.push(
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
            data.deleted_ids.clear();
            data.new_objs.clear();
        }
    };

public:
    ThreadData(AsyncHandler& instance, int thread,
               AsyncHandler::CallMap& cm, AsyncHandler::ObjectMap& om,
               AsyncHandler::TimerMap& tm, io_uring* ring) :
        ring(ring),
        proc_to_dat(cm),
        obj_info(om),
        timers(tm),
        instance(instance),
        thread(thread)
    {
    }

    Context begin_recording(uint64_t obj_id, uint64_t proc_id = numeric_limits<uint64_t>::max()) {
        return Context(*this, obj_id, proc_id);
    }

    void remove_child(uint64_t object_id, int target_tid) {
        deleted_ids.emplace_back(target_tid, object_id);
    }

    /**
     * @returns Thread ID of object, object id
     */
    tuple<int, uint64_t> init(ConstructorCall::Type f) {
        auto id = get_obj_id();

        // Attempt to stay on the same thread, with a lower threshold
        // due to the anticipation of having multiple function calls.
        {
            lock_guard lu(instance.per_thread_data[thread].stats);
            if (instance.per_thread_data[thread].load_avg < 0.8) {
                new_objs.emplace_back(thread, id, f);
                return {thread, id};
            }
        }

        int min_thread, min_thread_0 = -1; float min_amt = 1.0;

        // Need constant size for iteration
        lock_guard vec(instance.size_dec);
        for (size_t i = 0; i < instance.per_thread_data.size(); i++) {
            lock_guard t(instance.per_thread_data[i].stats);

            // Don't push to empty thread unless all working threads have too much utilization
            if (instance.per_thread_data[i].load_avg < min_amt) {
                if (instance.per_thread_data[i].load_avg > 0.0) {
                    min_amt = instance.per_thread_data[i].load_avg;
                    min_thread = i;
                } else {
                    min_thread_0 = i;
                }
            }
        }

        if (min_amt > 0.2 && min_thread_0 != -1) {
            new_objs.emplace_back(min_thread_0, id, f);
            return {min_thread_0, id};
        } else {
            new_objs.emplace_back(min_thread, id, f);
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
            lock_guard lu(instance.per_thread_data[thread].stats);
            if (instance.per_thread_data[thread].load_avg < 0.9) {
                procedure_calls.emplace_back(thread, target_object, id, f);
            }
        } else {
            int min_thread; float min_amt = 1.0;
            for (auto tid : target_tid) {
                lock_guard lu(instance.per_thread_data[tid].stats);
                if (instance.per_thread_data[tid].load_avg < min_amt) {
                    min_amt = instance.per_thread_data[tid].load_avg;
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

    uint64_t get_proc_id() {
        if (proc_id_curr == proc_id_base + 1000 || !p_init) {
            proc_id_base = proc_id_curr = 1000 * instance.get_proc_block();
            p_init = true;
        }
        return proc_id_curr++;
    }

    uint64_t get_obj_id() {
        if (obj_id_curr == obj_id_base + 1000 || !o_init) {
            obj_id_base = obj_id_curr = 1000 * instance.get_obj_block();
            o_init = true;
        }
        return obj_id_curr++;
    }

    uint64_t get_tim_id() {
        if (tim_id_curr == tim_id_base + 1000 || !t_init) {
            tim_id_base = tim_id_curr = 1000 * instance.get_obj_block();
            t_init = true;
        }
        return tim_id_curr++;
    }
};

void AsyncHandler::function_call(CallMap& proc_to_dat, ObjectMap& obj_info, FunctionCall& func, int tid) {
    {
        CallDataThreaded& cd = proc_to_dat[func.ti.proc_id];
        cd.assoc_obj = func.ti.obj_id;
        cd.back_notify = {func.ci.thread_id, func.ci.obj_id, func.ci.proc_id};
        cd.status = CallStatus::Running;
    }

    CallResponse resp = func.call(obj_info[func.ti.obj_id].ptr, func.ti.proc_id);
    per_thread_data[func.ci.thread_id].q.push(
        ProcedureUpdate {
            .type = PUType::StartConfirm,
            .resp = resp,
            .ci = CallerInfo {
                .thread_id = tid,
                .obj_id = func.ti.obj_id,
                .proc_id = func.ti.proc_id
            },
            .ti = TargetInfo {
                .obj_id = func.ci.obj_id,
                .proc_id = func.ci.proc_id
            }
        }
    );

    CallDataThreaded& cd = proc_to_dat[func.ti.proc_id];
    cd.op_hint = resp.op_hint;
    cd.description = resp.description;
    cd.status = resp.success ? CallStatus::Running : CallStatus::Degraded;

    if (resp.ret) {
        auto [failed, code] = resp.ret.value();
        cd.status = failed ? CallStatus::Failed : CallStatus::Finished;
        cd.return_code = code;

        // We don't iterate through back_notify since at this point only the notifiable entity is the
        // calling procedure at thread_id.
        per_thread_data[func.ci.thread_id].q.push(
            Data {
                .ci = CallerInfo {
                    .thread_id = tid,
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
                    .status = cd.status,
                    .return_code = code
                }
            }
        );
    }
}

void AsyncHandler::run(int tid) {
    CallMap proc_to_dat;
    ObjectMap obj_info;
    TimerMap timers;
    io_uring_cqe *cqe[128] = {nullptr};

    io_uring ring;
    if (io_uring_queue_init(512, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return;
    }

    // Communication object for events
    ThreadData datum(*this, tid, proc_to_dat, obj_info, timers, &ring);

    while (true) {
        __kernel_timespec ts ={
            .tv_sec = 10,
            .tv_nsec = 0
        };
        // Wait for completions
        if (io_uring_wait_cqes_min_timeout(&ring, cqe, 128, &ts, 200, nullptr) < 0) {
            continue;
        }

        // Multithreading support

        PerThread& tdata = per_thread_data[tid];

        variant<monostate, ConstructorCall, FunctionCall, ProcedureUpdate, Delete, Data> result;
        tdata.q.try_pop(result);

        visit(overloaded {
            [&] (ConstructorCall& construct) mutable {
                obj_info[construct.ti.obj_id].ptr->uring_data_.thread_data = &datum;

                auto ctx = datum.begin_recording(construct.ti.obj_id);
                obj_info.emplace(construct.ti.obj_id, ObjectDataThreaded {
                    .ptr = construct.constructor(),
                    .assoc_procs = {},
                    .parents = {{construct.ci.thread_id, construct.ci.obj_id}},
                    .children = {}
                });
            },
            [&, this] (FunctionCall& func) {
                obj_info[func.ti.obj_id].ptr->uring_data_.thread_data = &datum;

                auto ctx = datum.begin_recording(func.ti.obj_id, func.ti.proc_id);
                function_call(proc_to_dat, obj_info, func, tid);
            },
            [&] (ProcedureUpdate& upd) {
                obj_info[upd.ti.obj_id].ptr->uring_data_.thread_data = &datum;

                obj_info[upd.ti.obj_id].ptr->start_response(upd.ci.proc_id, upd.resp);
            },
            [&, this] (Delete& del) mutable {
                obj_info[del.ti.obj_id].ptr->uring_data_.thread_data = &datum;

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
                obj_info[on_data.ti.obj_id].ptr->uring_data_.thread_data = &datum;

                auto ctx = datum.begin_recording(
                    on_data.ti.obj_id, on_data.ti.proc_id);

                auto res = obj_info[on_data.ti.obj_id].ptr->on_yield(on_data.data);
                if (res) {
                    pair<bool, int> yielded = res.value();

                    proc_to_dat[on_data.ti.proc_id].status = yielded.first ? CallStatus::Failed : CallStatus::Finished;

                    Data completed_notification = {
                        .ci = CallerInfo {
                            .thread_id = tid,
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
            },
            [] (monostate) {

            }
        }, result);
    }
}

template <typename Obj, typename... Args>
size_t Event::i(Args... args) {
    auto f = [args...] mutable {
        return make_shared<Obj>(std::forward<Args>(args)...);
    };

    auto [thread, id] = uring_data_.thread_data->init(f);
    auto& vec_evs = uring_data_.sub_events[type_index(typeid(Obj))];

    auto it = find(vec_evs.begin(), vec_evs.end(), nullopt);
    if (it == vec_evs.end()) {
        vec_evs.emplace_back(EventInfo {id, {thread}, weak_ptr<Event>()});
        return vec_evs.size() - 1;
    } else {
        it->emplace(EventInfo {id, {thread}, weak_ptr<Event>()});
        return it - vec_evs.begin();
    }
    return 0;
}

template <typename Obj, typename... Args, typename... FnArgs>
uint64_t Event::c(size_t idx, CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args) {
    auto f = [args..., fun] (shared_ptr<Event> obj, uint64_t id) mutable -> CallResponse {
        return (static_pointer_cast<Obj>(obj).get()->*fun)(id, std::forward<Args>(args)...);
    };

    EventInfo& info = uring_data_.sub_events.at(type_index(typeid(Obj))).at(idx).value();
    return uring_data_.thread_data->call(info.thread_id, info.object_id, f);
}

template <typename Obj, typename... Args, typename... FnArgs>
uint64_t Event::c(CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args) {
    auto& evs_ty = uring_data_.sub_events.at(type_index(typeid(Obj)));
    auto it = find(evs_ty.begin(), evs_ty.end(), nullopt);
    if (it == evs_ty.end())
        throw runtime_error{"No event for call found."};

    return c(it - evs_ty.begin(), fun, args...);
}

template <typename... Args, typename... FnArgs>
void Event::c(int op, void(*liburing)(io_uring_sqe*, Args...), FnArgs... args) {
    auto f = [args..., liburing](io_uring_sqe* sqe) mutable {
        liburing(sqe, args...);
    };

    uring_data_.thread_data->call_uring(op, f);
}

template <typename Obj>
void Event::d(size_t idx) {
    auto opt = uring_data_.sub_events.at(type_index(typeid(Obj))).at(idx);
    EventInfo& ev = opt.value();

    uring_data_.thread_data->del(ev.thread_id, ev.object_id);
    uring_data_.sub_events[type_index(typeid(Obj))][idx] = nullopt;
}

uint64_t Event::timer(__kernel_timespec ts) {
    return uring_data_.thread_data->timer(ts);
}
