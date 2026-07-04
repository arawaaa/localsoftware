#pragma once

#include "defs.cpp"

struct alignas(hardware_destructive_interference_size) PerThread {
    typedef variant<ConstructorCall, FunctionCall, ProcedureUpdate, Delete, Data, RootStart> EventVariant;
    typedef oneapi::tbb::concurrent_bounded_queue<EventVariant> WorkQueue;
    WorkQueue q;

    mutex stats;
    bool idle = true;
    size_t num_ev;
    float load_avg = 0.0;
};

struct alignas(hardware_destructive_interference_size) PerThread2 {

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
