#pragma once

#include <cstdint>
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

    static AsyncHandler& getInstance(int ts = 1) {
        static AsyncHandler instance(ts);
        return instance;
    }

    AsyncHandler(const AsyncHandler&) = delete;
    AsyncHandler& operator=(const AsyncHandler&) = delete;

    // Do not queue any async functions in the constructor
    template <typename T, typename... Args>
    size_t initialize_dependent_event(Event* parent, Args&&... args) {
        // TODO Fix mutable
        auto construct = [args...] () mutable -> shared_ptr<Event> {
            return make_shared<T>(std::forward<Args>(args)...);
        };

        auto ev = make_shared<T>(std::forward<Args>(args)...);
        ev->uring_data_.outer_event = parent;
        for (auto [idx, shared_ptr] : views::enumerate(parent->uring_data_.events[type_index(typeid(T))])) {
            if (!shared_ptr) {
                parent->uring_data_.events[type_index(typeid(T))][idx] = ev;
                ev->uring_data_.id = idx;
                return idx;
            }
        }
        parent->uring_data_.events[type_index(typeid(T))].emplace_back(ev);
        ev->uring_data_.id = parent->uring_data_.events[type_index(typeid(T))].size() - 1;
        return parent->uring_data_.events[type_index(typeid(T))].size() - 1;
    }

    template <typename T, typename... Args>
    size_t initialize_root_event(Args&&... args) {
        root_events_[type_index(typeid(T))].emplace_back(make_shared<T>(std::forward<Args>(args)...));
        return root_events_[type_index(typeid(T))].size() - 1;
    }

    template <typename T>
    auto get_data(Event* parent, int idx, uint64_t id) {
        auto res = static_cast<T*>(parent->uring_data_.events[type_index(typeid(T))][idx].get())->get_data(id);
        using DataT = decltype(res.second);
        if (res.first.valid) {
            return optional<DataT>(std::move(res.second));
        }
        return optional<DataT>();
    }

    template <typename T, typename Method, typename... Args>
    pair<uint64_t, bool> call_dependent_function(Event* parent, int idx, Method method, Args&&... args) {
        uint64_t id = next_id_++;
        uint64_t old_running_id = running_id_;
        running_id_ = id;

        T* ev = static_cast<T*>(parent->uring_data_.events[type_index(typeid(T))][idx].get());

        {
            CallMap::accessor a;
            CallMap::accessor a2;
            if (call_map_.find(a, old_running_id)) {
                a->second.other_ids.push_back(running_id_);
            }
            if (call_map_.insert(a2, running_id_)) {
                a2->second.status = CallStatus::Running;
                a2->second.event = ev;
                a2->second.parent_task_id.push_back(old_running_id);
            }
        }

        CallResponse resp = (ev->*method)(id, std::forward<Args>(args)...);

        {
            CallMap::accessor a;
            if (call_map_.find(a, running_id_)) {
                if (id != 0 && (a->second.status == CallStatus::Finished || a->second.status == CallStatus::Failed)) {
                    propagation_queue_.push(id);
                } else if (!resp.success) {
                    a->second.status = CallStatus::Failed;
                }
                a->second.description = std::move(resp.description);
                a->second.op_hint = resp.op_hint;
            }
        }

        if (old_running_id != 0) {
            CallMap::accessor a;
            if (call_map_.find(a, old_running_id))
                a->second.event = parent;
        }

        running_id_ = old_running_id;
        return {id, resp.success};
    }

    template <typename T, typename Method, typename... Args>
    pair<uint64_t, bool> call_root_function(int idx, Method method, Args&&... args) {
        uint64_t id = next_id_++;
        uint64_t old_running_id = running_id_;
        running_id_ = id;

        T* ev = static_cast<T*>(root_events_[type_index(typeid(T))][idx].get());
        CallResponse resp = (ev->*method)(id, std::forward<Args>(args)...);

        CallMap::accessor a;
        call_map_.insert(a, id);
        a->second.status = resp.success ? CallStatus::Running : CallStatus::Failed;
        a->second.description = std::move(resp.description);
        a->second.op_hint = resp.op_hint;
        a->second.event = ev;

        running_id_ = old_running_id;
        return {id, resp.success};
    }

    // T1 and T2 *must* be distinct and they must both be initialized. T1 must have the specified type, and indices must be valid
    template <typename T1_from, typename T2_to, typename SubEvent>
    void move_subevents(Event* parent, size_t idx1, size_t idx2) {
        auto source = parent->uring_data_.events[type_index(typeid(T1_from))][idx1];
        auto to = parent->uring_data_.events[type_index(typeid(T2_to))][idx2];
        auto& svec = source->uring_data_.events[type_index(typeid(SubEvent))];
        auto& dvec = to->uring_data_.events[type_index(typeid(SubEvent))];
        auto s = to->uring_data_.events[type_index(typeid(SubEvent))].size();
        for_each(svec.begin(), svec.end(), [s] (shared_ptr<Event>& ptr) {
            ptr->uring_data_.id += s;
        });
        dvec.insert(dvec.end(), svec.begin(), svec.end());
        source->uring_data_.events[type_index(typeid(SubEvent))].clear();
    }

    template <typename T1_from, typename SubEvent>
    void move_subevents_up(Event* parent, size_t idx) {
        auto source = parent->uring_data_.events[type_index(typeid(T1_from))][idx];
        auto& svec = source->uring_data_.events[type_index(typeid(SubEvent))];
        auto& dvec = parent->uring_data_.events[type_index(typeid(SubEvent))];
        auto s = dvec.size();
        for_each(svec.begin(), svec.end(), [s] (shared_ptr<Event>& ptr) {
            ptr->uring_data_.id += s;
        });
        dvec.insert(dvec.end(), svec.begin(), svec.end());
        source->uring_data_.events[type_index(typeid(SubEvent))].clear();
    }

    void finalize_current_task(bool failed, int return_code) {
        if (running_id_ == 0) return;
        CallMap::accessor a;
        call_map_.find(a, running_id_);

        a->second.status = failed ? CallStatus::Failed : CallStatus::Finished;
        a->second.return_code = return_code;
    }

    void run(io_uring* ring, int tid);

    template <typename F, typename... Args>
    void cache_call(Event* ev, int op, F&& func, Args&&... args) {
        pending_events_.emplace_back(ev, op, running_id_, [f = std::forward<F>(func), ...args = std::forward<Args>(args)](io_uring_sqe* sqe) mutable {
            f(sqe, args...);
        });
    }

    template <typename T>
    void free_child_event_for_taskid(Event* parent, uint64_t taskid) {
        // TODO schedule delete laters on the proper thread and make threads own the "main" shared_ptr, and make the Event pointers weak
        CallMap::accessor a;
        call_map_.find(a, taskid);
        parent->uring_data_.events[type_index(typeid(T))][a->second.event->uring_data_.id].reset();
    }

    uint64_t set_timer(__kernel_timespec ts, uint64_t timer = 0) {
        if (timer == 0) {
            auto timer = next_timer_++;
            pending_timers_.emplace_back(false, ts, timer, running_id_);
            return timer;
        } else {
            pending_timers_.emplace_back(true, ts, timer, running_id_);
            return timer;
        }
    }

    void cancel_timer(uint64_t id) {
        pending_timers_.emplace_back(true, __kernel_timespec{.tv_sec=0, .tv_nsec=0}, id, running_id_);
    }

    void submit_events(io_uring* ring) {
        if (pending_events_.empty()) return;
        for (auto& item : pending_events_) {
            Event* ev = get<0>(item);
            int op = get<1>(item);
            uint64_t rid = get<2>(item);
            auto& func = get<3>(item);

            io_uring_sqe* sqe = io_uring_get_sqe(ring);
            if (sqe) {
                func(sqe);
                IoUringAttached* data = new IoUringAttached;
                data->data.emplace<EventData>(EventData{op, rid, ev});
                io_uring_sqe_set_data(sqe, data);
            }
        }

        for (auto [update, ts, id, rid] : pending_timers_) {
            if (update) {
                TimerMap::accessor a;
                if (!timer_map_.find(a, id)) continue;
                bool zeroed = ts.tv_nsec == 0 && ts.tv_sec == 0;
                io_uring_sqe* sqe = io_uring_get_sqe(ring);
                IoUringAttached* data = new IoUringAttached;
                io_uring_sqe_set_data(sqe, data);
                if (zeroed) {
                    data->data.emplace<TimerUpdate>(TimerUpdate {true});
                    io_uring_prep_timeout_remove(sqe, (__u64)a->second, 0);
                } else {
                    data->data.emplace<TimerUpdate>(TimerUpdate {false});
                    io_uring_prep_timeout_update(sqe, &ts, (__u64)a->second, 0);
                }
            } else {
                io_uring_sqe* sqe = io_uring_get_sqe(ring);
                io_uring_prep_timeout(sqe, &ts, 0, 0);
                IoUringAttached* data = new IoUringAttached;
                data->data.emplace<Timer>(Timer {id, rid});
                io_uring_sqe_set_data(sqe, data);
                TimerMap::accessor a;
                timer_map_.insert(a, {id, data});
            }
        }
        io_uring_submit(ring);
        pending_events_.clear();
        pending_timers_.clear();
    }

    // No effect if the child has already completed
    void attach_child(uint64_t id) {
        CallMap::accessor rid, child;
        if (call_map_.find(child, id)) {
            child->second.parent_task_id.push_back(running_id_);
            child.release();
            call_map_.find(rid, running_id_);
            rid->second.other_ids.push_back(id);
        }
    }



    SSL_CTX* get_tls_ctx() {
        return tls_ctx_;
    }

private:
    void consume_event(uint64_t taskid) {
        // Copy parent and child task lists to avoid deadlock
        list<uint64_t> parent_tasks, child_tasks;
        {
            CallMap::const_accessor ca;
            if (!call_map_.find(ca, taskid)) return;
            parent_tasks = ca->second.parent_task_id;
            child_tasks = ca->second.other_ids;
        }

        for (auto id : parent_tasks) {
            CallMap::accessor a;
            if (!call_map_.find(a, id)) continue;
            a->second.other_ids.remove(taskid);
        }

        for (auto id : child_tasks) {
            consume_event(id);
        }

        call_map_.erase(taskid);
    }

    void process_propagation_queue() {
        while (!propagation_queue_.empty()) {
            uint64_t finished_id = propagation_queue_.front();
            propagation_queue_.pop();

            // HACK TODO Make this actually correct, threading friendly, with work queues
            CallMap::accessor a;
            if (!call_map_.find(a, finished_id))
                continue;
            auto& data = a->second;
            a.release();

            for (auto parent_id : data.parent_task_id) {
                CallMap::accessor a;
                call_map_.find(a, parent_id);
                if (a->second.event) {
                    auto ev = a->second.event;
                    a.release();
                    uint64_t old_rid = running_id_;
                    running_id_ = parent_id;

                    ChildTaskCompletion comp = {running_id_, finished_id, data.status, data.return_code};

                    ev->on_new_data(ID_DEFAULT, comp);
                    CallMap::const_accessor parent;
                    call_map_.find(parent, parent_id);

                    if (parent->second.status == CallStatus::Finished || parent->second.status == CallStatus::Failed) {
                        propagation_queue_.push(parent_id);
                    }

                    running_id_ = old_rid;
                }
            }

            consume_event(finished_id);
        }
    }

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

    // REMOVE
    vector<tuple<Event*, int, uint64_t, function<void(io_uring_sqe*)>>> pending_events_;
    vector<tuple<bool, __kernel_timespec, uint64_t, uint64_t>> pending_timers_;
    uint64_t next_id_ = 1;
    uint64_t next_timer_ = 1;
    uint64_t running_id_ = 0;
    queue<uint64_t> propagation_queue_;

    atomic<uint64_t> proc_block_ = 0;
    atomic<uint64_t> obj_block_ = 0;

    struct alignas(hardware_destructive_interference_size) PerThread {
        typedef variant<monostate, ConstructorCall, FunctionCall, ProcedureUpdate, Delete, Data> EventVariant;
        typedef oneapi::tbb::concurrent_bounded_queue<EventVariant> WorkQueue;
        WorkQueue q;

        mutex stats;
        bool idle = true;
        size_t num_ev;
        float load_avg = 0.0;
    };

    vector<PerThread> per_thread_data;

    typedef oneapi::tbb::concurrent_hash_map<uint64_t, CallData> CallMap;
    typedef oneapi::tbb::concurrent_hash_map<uint64_t, IoUringAttached*> TimerMap;
    TimerMap timer_map_;
    CallMap call_map_;
    map<type_index, vector<shared_ptr<Event>>> root_events_;
};

class ThreadData {
    int thread;
    uint64_t proc_id_base, proc_id_curr; // Upper bound of block is id_base + 1000
    uint64_t obj_id_base, obj_id_curr;
    uint64_t source_object, source_proc;

    list<pair<int, uint64_t>> procedure_calls;
    list<function<void(io_uring_sqe*)>> pending_uring;

    AsyncHandler& instance;

    class Context {
        ThreadData& data;
    public:
        Context(ThreadData& data, uint64_t object_id, uint64_t proc_id) : data(data)
        {
            data.source_object = object_id;
            data.source_proc = proc_id;
        }

        list<pair<int, uint64_t>> get_procedure_calls() {
            return data.procedure_calls;
        }

        ~Context() {
            data.procedure_calls.clear();
            data.pending_uring.clear();
        }
    };

public:
    ThreadData(AsyncHandler& instance, int thread) :
        instance(instance),
        thread(thread)
    {
        get_proc_block();
    }

    Context begin_recording(uint64_t obj_id, uint64_t proc_id) {
        return Context(*this, obj_id, proc_id);
    }

    template <typename Obj, typename... FnArgs, typename... Args>
    uint64_t call(unordered_set<int> target_tid, uint64_t target_object, CallResponse(Obj::*fun)(uint64_t, Args...), Args... args) {
        // Locally get the id by preallocation of a block per thread
        uint64_t id = proc_id_curr++;

        auto f = [args..., fun, id] (shared_ptr<Event> obj) mutable -> CallResponse {
            return (static_pointer_cast<Obj>(obj).get()->*fun)(id, std::forward<Args>(args)...);
        };

        FunctionCall callinfo = {
            .call = f,
            .ci = CallerInfo {
                .thread_id = thread,
                .obj_id = source_object,
                .proc_id = source_proc
            },
            .object_id = target_object,
            .procedure_id = id
        };

        // Usually child events will be on the same thread, so this will minimize thrashing
        // If a child event is on multiple threads, select the one with the lowest load average
        // TODO Auto-scaling child events if they allow multithreading
        if (target_tid.contains(thread)) {
            // Try to keep ourselves on the core-local cache line
            lock_guard lu(instance.per_thread_data[thread].stats);
            if (instance.per_thread_data[thread].load_avg < 0.9) {
                instance.per_thread_data[thread].q.push(callinfo);
            }
        } else {
            int min_thread; float min_amt = 1.0;
            for (auto tid : target_tid) {
                if (tid == thread) continue;
                lock_guard lu(instance.per_thread_data[tid].stats);
                if (instance.per_thread_data[tid].load_avg < min_amt) {
                    min_amt = instance.per_thread_data[tid].load_avg;
                    min_thread = tid;
                }
            }
            instance.per_thread_data[min_thread].q.push(callinfo);
        }

        if (proc_id_curr == proc_id_base + 1000) {
            get_proc_block();
        }
        return id;
    }

    void get_proc_block() {
        proc_id_base = proc_id_curr = 1000 * AsyncHandler::self().get_proc_block();
    }

    void get_obj_block() {
        obj_id_base = obj_id_curr = 1000 * AsyncHandler::self().get_obj_block();
    }
};

void AsyncHandler::run(io_uring* ring, int tid) {
    unordered_map<uint64_t, CallDataThreaded> proc_to_dat;
    unordered_map<uint64_t, shared_ptr<Event>> id_to_ev;
    io_uring_cqe *cqe[128] = {nullptr};

    // Communication object for events
    ThreadData datum(*this, tid);

    while (true) {
        // Ensure pending submissions are sent
        submit_events(ring);

        __kernel_timespec ts ={
            .tv_sec = 10,
            .tv_nsec = 0
        };
        // Wait for completions
        if (io_uring_wait_cqes_min_timeout(ring, cqe, 128, &ts, 200, nullptr) < 0) {
            continue;
        }

        // Multithreading support
        goto jump;
        {
        PerThread& tdata = per_thread_data[tid];

        variant<monostate, ConstructorCall, FunctionCall, ProcedureUpdate, Delete, Data> result;
        tdata.q.try_pop(result);

        visit(overloaded {
            [&id_to_ev] (ConstructorCall& construct) mutable {
                id_to_ev.emplace(construct.object_id, construct.constructor());
            },
            [this, &datum, &proc_to_dat, &id_to_ev] (FunctionCall& func) {
                auto ctx = datum.begin_recording(func.object_id, func.procedure_id);
                CallResponse resp = func.call(id_to_ev[func.object_id]);
                per_thread_data[func.ci.thread_id].q.push(
                    ProcedureUpdate {.type = PUType::StartConfirm, .object_id = func.ci.obj_id, .resp = resp}
                );
                CallDataThreaded& cd = proc_to_dat[func.procedure_id];
                cd.status = resp.success ? CallStatus::Running : CallStatus::Failed;
                if (resp.ret) {
                    auto [failed, code] = resp.ret.value();
                    cd.status = failed ? CallStatus::Failed : CallStatus::Finished;
                    per_thread_data[func.ci.thread_id].q.push(
                        Data {
                            .object_id = func.ci.obj_id,
                            .data = ChildTaskCompletion {
                                .calling_id = func.ci.proc_id,
                                .task_id = func.procedure_id,
                                .status = cd.status,
                                .return_code = code,
                            }
                        }
                    );
                }

            },
            [&id_to_ev] (ProcedureUpdate& upd) {
                id_to_ev[upd.object_id]->procedure_update(upd.type, upd.resp);
            },
            [&id_to_ev] (Delete& del) mutable {
                id_to_ev.erase(del.object_id);
            },
            [] (Data& on_data) {

            },
            [] (monostate) {

            }
        }, result);

        }
        jump:

        int i = 0;
        for (auto ptr = cqe; *ptr && ptr < cqe + 128; ptr++) {
            IoUringAttached* uringdata = reinterpret_cast<IoUringAttached*>(io_uring_cqe_get_data(*ptr));
            if (holds_alternative<EventData>(uringdata->data)) {
                EventData& data = std::get<EventData>(uringdata->data);
                Event* ev = data.event;
                int op = data.op;
                uint64_t rid = data.running_id;

                uint64_t old_rid = running_id_;
                running_id_ = rid;
                if (!call_map_.count(running_id_)) { // All class operations occur on the same thread, so this is safe
                    delete uringdata;
                    running_id_ = old_rid;
                    i++;
                    continue;
                }

                ev->on_new_data(op, IoUringResult{running_id_, (*ptr)->res});

                if (rid != 0) {
                    CallMap::const_accessor a;
                    if (call_map_.find(a, rid) &&
                        (a->second.status == CallStatus::Finished
                        || a->second.status == CallStatus::Failed)) {
                        propagation_queue_.push(rid);
                    }
                }

                running_id_ = old_rid;
            } else if (holds_alternative<Timer>(uringdata->data)) {
                Timer& data = std::get<Timer>(uringdata->data);
                int res = (*ptr)->res;
                if (res == -ETIME) {
                    CallMap::accessor a;
                    if (call_map_.find(a, data.running_id)) {
                        Event* ptr = a->second.event;
                        a.release();

                        uint64_t old_rid = running_id_;
                        running_id_ = data.running_id;

                        ptr->on_new_data(0, Timeout {data.timer_id});


                        if (running_id_ != 0 && call_map_.find(a, data.running_id) &&
                        (a->second.status == CallStatus::Finished
                        || a->second.status == CallStatus::Failed)) {
                            propagation_queue_.push(data.running_id);
                        }
                        running_id_ = old_rid;
                    }
                }
                timer_map_.erase(data.timer_id);
            }
            i++;
            delete uringdata;
        }

        process_propagation_queue();

        io_uring_cq_advance(ring, i);
    }
}

template <typename Obj, typename... Args>
uint64_t Event::c(size_t idx, CallResponse(Obj::*fun)(uint64_t, Args...), Args... args) {
    EventInfo& info = uring_data_.sub_events[type_index(typeid(Obj))][idx];
    return uring_data_.thread_data->call(info.thread_id, info.object_id, fun, args...);
}

template <typename Obj, typename... Args>
uint64_t Event::c(CallResponse(Obj::*fun)(uint64_t, Args...), Args... args) {
    return c(0, fun, args...);
}
