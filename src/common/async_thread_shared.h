#pragma once

#include <cstdint>
#include <map>
#include <functional>
#include <coroutine>

#include "coroutine/task.h"
#include "defs.h"

// Workqueue types

struct Root {
    ConstructFunc constructor;
    function<shared_ptr<task>(shared_ptr<Event>)> fn;
};

struct CqeReady {
    uint64_t id;
    int result;
};

// ThreadData internal

struct alignas(hardware_destructive_interference_size) PerThread {
    typedef variant<Root, CqeReady> EventVariant;
    typedef oneapi::tbb::concurrent_bounded_queue<EventVariant> WorkQueue;
    WorkQueue q;

    mutex stats;
    bool idle = true;
    size_t num_ev;
    float load_avg = 0.0;
};

struct IdBlocks {
    atomic<uint64_t> proc_block_ = 0;
    atomic<uint64_t> obj_block_ = 0;
    atomic<uint64_t> timer_block_ = 0;

    IdBlocks() {};
    IdBlocks(IdBlocks&) = delete;
    IdBlocks& operator=(IdBlocks&) = delete;
};

// For uring only
struct Waiters {
    int result;
    coroutine_handle<> handle;
};
