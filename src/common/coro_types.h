#pragma once

#include <coroutine>

#include "async_thread_shared.h"

class ThreadData;

/*
 * NOTE: Single awaiter only
 *
 * Class for awaiting io_uring completions
 *
 * Important: must check if cqe has been received on calling await_suspend,
 * and must also check if awaitable has been suspended on receiving completion
 *
 * The awaitable must not be destroyed while it is in use!
 */
struct readiness_api_awaitable {
    int ret;
    uint64_t id;
    // This thread stays alive until all awaitables associated to it
    // are finished (destroyed). Can be shifted on start/suspend
    ThreadData* data;

    readiness_api_awaitable(function<void(io_uring_sqe*)> sfunc);

    readiness_api_awaitable(readiness_api_awaitable& other) = delete;
    readiness_api_awaitable& operator=(readiness_api_awaitable& other) = delete;

    // Moved-from object should be considered invalid. Do not move an awaitable that is already suspended, return value will be wrong!
    readiness_api_awaitable(readiness_api_awaitable&& other);

    readiness_api_awaitable& operator=(readiness_api_awaitable&& other);

    ~readiness_api_awaitable();

    bool await_ready() { return false; }

    void await_suspend(coroutine_handle<> handle);

    // liburing functions return only void, but as we co_await for a completion, we must convey the return code
    int await_resume();
};

/*
 * co_await on this object to unpause when the first awaitable finishes.
 * Remaining requests will be cancelled.
 *
 * This is a cancellation node, and will propagate cancellations to downstream
 * AnyOf/All and uring interface awaitables
 *
 * Not thread safe
 *
 * Approach: we start child subtasks by calling a function on them, that is
 * functionally equal to co_await on them (as we have a lazy execution model).
 * This will start/queue the tasks. When the tasks return, they will handoff
 * control to this coroutine. When we then call await_resume, we will check
 * each awaitable, and return the stored value for the first complete one.
 */
struct AnyOf {

};
