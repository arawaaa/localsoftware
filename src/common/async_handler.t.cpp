#pragma once
#include "async_handler.h"

template <typename Obj, typename... Args>
void AsyncHandler::compute_root(function<shared_ptr<task>(shared_ptr<Event>)> fn, Args&&... args) {
    auto f = [args...] mutable {
        return make_shared<Obj>(std::forward<Args>(args)...);
    };

    Root callinfo {
        .constructor = f,
        .fn = fn
    };
    per_thread_data[pt_idx].q.push(callinfo);
}
