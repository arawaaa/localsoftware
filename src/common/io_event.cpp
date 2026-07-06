#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <unordered_set>
#include <typeindex>
#include <unordered_map>
#include <liburing.h>
#include <optional>
#include <type_traits>
#include <iostream>

#include "file.cpp"
#include "defs.cpp"
#include "async_thread_shared.cpp"

using namespace std;
extern thread_local shared_ptr<IoUringData> context;

class ThreadData;

/**
 * @brief Base class for all io_uring events.
 */
class Event {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    struct LocalData {
        int id;
        ThreadData* thread_data;
        Event* outer_event;
    };

    /**
     * For constructors: do not call any delegated class functions. They will not have a context
     * to run within and will not be run!
     */
    explicit Event(vector<shared_ptr<File>> file) : files_(file) {
        init();
    }

    Event(Event&) = delete;
    Event operator=(Event&) = delete;

    virtual void construct_with_global() = 0;

    Event() {
        init();
    }

    virtual ~Event();

    /**
     * @brief Handle data yielded from a delegated operation
     * @returns If task complete, pair<failure, ret_code>, if need to continue, nullopt
     */
    virtual optional<pair<bool, int>> on_yield(EventType event) = 0;

    /**
     * @brief Get string description for class
     */
    virtual string get_info() const = 0;

    /**
     * @brief Whether the class can be run on multiple threads
     */
    constexpr virtual bool is_thread_safe() {
        return false;
    }

    /**
     * @brief Called on successful start of a function call
     *
     * Do not start any non-direct functions from this function
     */
    virtual void start_response(uint64_t, CallResponse) { };

    /**
     * @brief Call a function in a subclass, automatically choosing the thread,
     * minimizing excessive minimizing out-of-current-thread calls
     *
     * Definition in AsyncHandler: depends on ThreadData
     */
    template <typename Obj, typename... Args, typename... FnArgs>
    uint64_t c(size_t idx, CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args) {
        auto f = [args..., fun] (shared_ptr<Event> obj, uint64_t id) mutable -> CallResponse {
            return (static_pointer_cast<Obj>(obj).get()->*fun)(id, std::forward<Args>(args)...);
        };

        lock_guard lu(uring_data_->mut);
        uint64_t local_id = uring_data_->local_proc_id++;

        function_.emplace_back(EventQueuedFunction {
            .idx = type_index(typeid(Obj)),
            .vidx = idx,
            .local_id = local_id,
            .fun = f
        });

        return local_id;
    }

    template <typename Obj, typename... Args, typename... FnArgs>
    uint64_t c(CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args) {
        auto& evs_ty = uring_data_->sub_events.at(type_index(typeid(Obj)));
        auto it = find_if_not(evs_ty.begin(), evs_ty.end(), [](auto& ev) {return ev == nullopt;});
        if (it == evs_ty.end())
            throw runtime_error{"No event for call found."};

        return c(it - evs_ty.begin(), fun, args...);
    }

    // Overload for liburing calls
    template <typename... Args, typename... FnArgs>
    void c(int op, void(*liburing)(io_uring_sqe*, Args...), FnArgs... args) {
        auto f = [args..., liburing](io_uring_sqe* sqe) mutable {
            liburing(sqe, args...);
        };

        uring_.emplace_back(EventQueuedUring {
            .op = op,
            .fun = f
        });
    }

    // TODO RAII handle, possibly with C++26 reflection
    // With functions that return "Promise" objects that lambdas can be attached to
    template <typename Obj, typename... Args>
    size_t i(Args... args) {
        auto& vec_evs = uring_data_->sub_events[type_index(typeid(Obj))];
        auto it = find(vec_evs.begin(), vec_evs.end(), nullopt);
        size_t loc = it - vec_evs.begin();

        vec_evs.emplace(it, EventInfo {nullopt, weak_ptr<Event>()});

        auto f = [&, args..., loc] mutable {
            auto o =  make_shared<Obj>(std::forward<Args>(args)...);
            // WARNING weak ptr should not be used prior to remote initialization, can be guaranteed when a function returns
            uring_data_->sub_events[type_index(typeid(Obj))][loc]->event = o;
            return o;
        };

        construct_.emplace_back(EventQueuedConstruct {
            .idx = type_index(typeid(Obj)),
            .vidx = loc,
            .fun = f
        });

        return loc;
    }

    template <typename Obj>
    void d(size_t idx) {
        auto opt = uring_data_->sub_events.at(type_index(typeid(Obj))).at(idx);
        EventInfo& ev = opt.value();

        delete_.emplace_back(EventQueuedDelete {
            .thread = ev.locator->thread_id,
            .obj_id = ev.locator->object_id
        });
        uring_data_->sub_events[type_index(typeid(Obj))][idx] = nullopt;
    }

    void attach(uint64_t id) {
        attach_.emplace_back(EventQueuedAttach {
            .target_local_id = id
        });
    }

    uint64_t timer(__kernel_timespec ts) {
        uint64_t id = uring_data_->local_timer_id++;

        timer_.emplace_back(EventQueuedTimer {
            .local_id = id,
            .time = ts
        });
        return id;
    }

    void cancel_timer(uint64_t timerid) {
        timer_.emplace_back(EventQueuedTimer {
            .local_id = timerid,
            .time = {0, 0}
        });
    }

    // Directly accesses the function in target object. No synchronization - dangerous
    // Remove this and replace with promise objects
    template <typename R, typename Obj, typename... Args>
    optional<R> direct_access(size_t idx, R(Obj::*fun)(Args...), Args... args) {
        shared_ptr<Obj> ptr = static_pointer_cast<Obj>(
            uring_data_->sub_events.at(type_index(typeid(Obj))).at(idx).value().event.lock());
        if (!ptr) return nullopt;
        return (ptr.get()->*fun)(args...);
    }

    void map_event_data(EventType& event) {
        visit(overloaded {
            [this](ChildTaskCompletion& child) {
                child.task_id = uring_data_->global_proc_to_local_proc[child.task_id];
            },
            [this](Timeout& time) {
                time.timer_id = uring_data_->global_tim_to_local_tim[time.timer_id];
            },
            [this](CallStarted& call) {
                call.procedure_id = uring_data_->global_proc_to_local_proc[call.procedure_id];
            },
            [](auto&) {
            }
        }, event);
    }

    shared_ptr<IoUringData> uring_data_;
    LocalData local_data_;

    template <typename T>
    list<T>& get_queued() {
        if constexpr (is_same_v<T, EventQueuedConstruct>) {
            return construct_;
        } else if constexpr (is_same_v<T, EventQueuedFunction>) {
            return function_;
        } else if constexpr (is_same_v<T, EventQueuedUring>) {
            return uring_;
        } else if constexpr (is_same_v<T, EventQueuedTimer>) {
            return timer_;
        } else if constexpr (is_same_v<T, EventQueuedDelete>) {
            return delete_;
        } else if constexpr (is_same_v<T, EventQueuedAttach>) {
            return attach_;
        }
    }

    void clear() {
        construct_.clear();
        function_.clear();
        uring_.clear();
        timer_.clear();
        delete_.clear();
        attach_.clear();
    }

    void global_to_local(uint64_t global, uint64_t local) {
        uring_data_->global_proc_to_local_proc[global] = local;
        uring_data_->local_proc_to_global_proc[local] = global;
    }

    void global_to_local_tim(uint64_t global, uint64_t local) {
        uring_data_->global_tim_to_local_tim[global] = local;
        uring_data_->local_tim_to_global_tim[local] = global;
    }

    uint64_t translate_proc_local(uint64_t local) { if (uring_data_->local_proc_to_global_proc.contains(local)) return uring_data_->local_proc_to_global_proc.at(local); else return numeric_limits<uint64_t>::max(); }

    uint64_t translate_proc_global(uint64_t global) { if (uring_data_->global_proc_to_local_proc.contains(global)) return uring_data_->global_proc_to_local_proc.at(global); else return numeric_limits<uint64_t>::max(); }

    uint64_t translate_tim_local(uint64_t local) { if (uring_data_->local_tim_to_global_tim.contains(local)) return uring_data_->local_tim_to_global_tim.at(local); else return numeric_limits<uint64_t>::max(); }

    uint64_t translate_tim_global(uint64_t global) { if (uring_data_->global_tim_to_local_tim.contains(global)) return uring_data_->global_tim_to_local_tim.at(global); else return numeric_limits<uint64_t>::max(); }

    optional<EventInfo>& get_evinfo(type_index ti, size_t vi) {
        return uring_data_->sub_events[ti][vi];
    }
protected:
    vector<shared_ptr<File>> files_;

private:
    void init() {
        if (context) {
            uring_data_ = context;
        } else {
            uring_data_ = make_shared<IoUringData>();
        }
    }

    list<EventQueuedConstruct> construct_;
    list<EventQueuedFunction> function_;
    list<EventQueuedUring> uring_;
    list<EventQueuedTimer> timer_;
    list<EventQueuedDelete> delete_;
    list<EventQueuedAttach> attach_;
};

inline Event::~Event() = default;
