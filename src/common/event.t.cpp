#pragma once
#include "event.h"

#include <liburing.h>

#include "defs.h"
#include "coro_types.h"

template <typename... Args, typename... FnArgs>
readiness_api_awaitable Event::c(void(*liburing)(io_uring_sqe*, Args...), FnArgs... args) {
    auto f = [args..., liburing](io_uring_sqe* sqe) mutable {
        liburing(sqe, args...);
    };

    return readiness_api_awaitable(f);
}
