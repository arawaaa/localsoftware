#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <liburing.h>

#include "file.h"
#include "defs.h"
#include "async_thread_shared.h"
#include "coroutine/readiness.h"

using namespace std;
extern thread_local uint64_t uring_id;

/**
 * @brief Base class for all io_uring events.
 */
class Event {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    /**
     * Event managed subclasses can be initialized and called in constructor body,
     * as the uring_data_ optionally shared context is available
     */
    explicit Event(vector<shared_ptr<File>> file);

    Event();

    // co_await liburing calls
    template <typename... Args, typename... FnArgs>
    readiness_api_awaitable c(void(*liburing)(io_uring_sqe*, Args...), FnArgs... args);

protected:
    vector<shared_ptr<File>> files_;
};

#include "event.t.cpp"
