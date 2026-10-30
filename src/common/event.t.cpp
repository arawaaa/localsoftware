#pragma once
#include "event.h"

#include <cstdint>
#include <memory>
#include <map>
#include <vector>
#include <typeindex>
#include <unordered_map>
#include <liburing.h>
#include <optional>
#include <type_traits>

#include "file.h"
#include "defs.h"
#include "async_thread_shared.h"
#include "coro_types.h"

template <typename Obj, typename... Args, typename... FnArgs>
uint64_t Event::c(size_t idx, CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args) {
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

    uring_.emplace_back(EventQueuedUring {
        .op = op,
        .fun = f
    });
}

template <typename... Args, typename... FnArgs>
readiness_api_awaitable Event::ca(void(*liburing)(io_uring_sqe*, Args...), FnArgs... args) {
    auto f = [args..., liburing](io_uring_sqe* sqe) mutable {
        liburing(sqe, args...);
    };

    return readiness_api_awaitable(f);
}

template <typename Obj, typename... Args>
size_t Event::i(Args... args) {
    return construct<Obj>(false, args...);
}

template <typename Obj, typename... Args>
size_t Event::i_s(Args... args) {
    return construct<Obj>(true, args...);
}

template <typename Obj>
void Event::d(size_t idx) {
    auto& opt = uring_data_->sub_events.at(type_index(typeid(Obj))).at(idx);
    EventInfo& ev = opt.value();

    delete_.emplace_back(EventQueuedDelete {
        .thread = ev.locator->thread_id,
        .obj_id = ev.locator->object_id
    });
    uring_data_->sub_events[type_index(typeid(Obj))][idx] = nullopt;
}

template <typename R, typename Obj, typename... Args>
optional<R> Event::direct_access(size_t idx, R(Obj::*fun)(Args...), Args... args) {
    shared_ptr<Obj> ptr = static_pointer_cast<Obj>(
        uring_data_->sub_events.at(type_index(typeid(Obj))).at(idx).value().event.lock());
    if (!ptr) return nullopt;
    return (ptr.get()->*fun)(args...);
}

template <typename T>
list<T>& Event::get_queued() {
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

template <typename Obj, typename... Args>
size_t Event::construct(bool shared, Args... args) {
    auto& vec_evs = uring_data_->sub_events[type_index(typeid(Obj))];
    auto it = find(vec_evs.begin(), vec_evs.end(), nullopt);
    size_t loc = it - vec_evs.begin();

    if (it == vec_evs.end()) {
        vec_evs.emplace_back(EventInfo {nullopt, weak_ptr<Event>()});
    } else {
        it->emplace(EventInfo {nullopt, weak_ptr<Event>()});
    }

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

    if (shared) {
        construct_.back().uring_data = uring_data_;
    }

    return loc;
}
