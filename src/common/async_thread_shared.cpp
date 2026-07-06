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

struct IdBlocks {
    atomic<uint64_t> proc_block_ = 0;
    atomic<uint64_t> obj_block_ = 0;
    atomic<uint64_t> timer_block_ = 0;

    IdBlocks() {};
    IdBlocks(IdBlocks&) = delete;
    IdBlocks& operator=(IdBlocks&) = delete;
};

struct EventInfo {
    struct Locator {
        uint64_t object_id;
        unordered_set<int> thread_id;
    };
    // Filled in by ThreadData
    optional<Locator> locator;
    weak_ptr<Event> event;
};

// In single-thread case, low overhead due to futex
// Lock shared when reading, lock unique when writing
struct IoUringData {
    unordered_map<type_index, vector<optional<EventInfo>>> sub_events;
    map<uint64_t, type_index> awaiting_resolve;
    uint64_t local_proc_id = 0;
    uint64_t local_timer_id = 0;
    // Will get rid of id translation when start using promises
    map<uint64_t, uint64_t> global_proc_to_local_proc;
    map<uint64_t, uint64_t> local_proc_to_global_proc;
    map<uint64_t, uint64_t> global_tim_to_local_tim;
    map<uint64_t, uint64_t> local_tim_to_global_tim;
    shared_mutex mut;
};
