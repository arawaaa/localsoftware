#pragma once

#include "defs.cpp"

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

// Event queue types

struct EventQueuedConstruct {
    type_index idx;
    size_t vidx;
    ConstructFunc fun;
    shared_ptr<IoUringData> uring_data = shared_ptr<IoUringData>();
};

struct EventQueuedFunction {
    type_index idx;
    size_t vidx;
    uint64_t local_id;
    FunctionFunc fun;
};

struct EventQueuedUring {
    int op;
    function<void(io_uring_sqe*)> fun;
};

struct EventQueuedTimer {
    uint64_t local_id;
    __kernel_timespec time;
};

struct EventQueuedDelete {
    unordered_set<int> thread;
    uint64_t obj_id;
};

struct EventQueuedAttach {
    uint64_t target_local_id;
};


// Workqueue types

struct CallResponse {
    string description;
    bool success;
    optional<pair<bool, int>> ret;
    uint32_t op_hint;
};

struct CallerInfo {
    int thread_id;
    uint64_t obj_id;
    uint64_t proc_id;
};

struct TargetInfo {
    uint64_t obj_id;
    uint64_t proc_id;
};

struct RootStart {
    ConstructFunc constructor;
    FunctionFunc init;
    TargetInfo ti;
};

struct ConstructorCall {
    ConstructFunc constructor;
    CallerInfo ci;
    TargetInfo ti;
    // Type erased pointer for shared context container
    shared_ptr<IoUringData> uring_data;
};

struct FunctionCall {

    FunctionFunc call;
    CallerInfo ci;
    TargetInfo ti;
};

enum class PUType : int {
    StartConfirm,
    Yield
};

struct ProcedureUpdate {
    PUType type;
    CallResponse resp;
    CallerInfo ci;
    TargetInfo ti;
};

struct Delete {
    CallerInfo ci;
    TargetInfo ti;
};

struct Data {
    CallerInfo ci;
    TargetInfo ti;
    EventType data;
};

// ThreadData internal

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
