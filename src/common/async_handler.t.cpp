#pragma once
#include "async_handler.h"

template <typename Obj, typename... Args>
void AsyncHandler::initialize_root_event(Args&&... args) {
    auto f = [args...] mutable {
        return make_shared<Obj>(std::forward<Args>(args)...);
    };

    auto c = [](shared_ptr<Event> ptr, uint64_t id) -> CallResponse {
        return static_pointer_cast<Obj>(ptr)->init(id);
    };

    RootStart callinfo {
        .constructor = f,
        .init = c,
        .ti = TargetInfo {
            .obj_id = get_obj_id(),
            .proc_id = get_proc_id()
        }
    };

    per_thread_data[pt_idx].q.push(callinfo);
}
