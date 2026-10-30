#pragma once

#include <coroutine>

#include "async_thread_shared.h"

class ThreadData;

struct readiness_api_awaitable {
    int ret;
    uint64_t id;
    ThreadData* data;
    list<Waiters::awaits>::iterator it;

    readiness_api_awaitable(function<void(io_uring_sqe*)> sfunc);
    bool await_ready() { return false; }



    // liburing functions return only void, but as we co_await for a completion, we must convey the return code
    int await_resume();
};
