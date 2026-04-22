#pragma once

#include <liburing.h>
#include <string>
#include <list>
#include <cstdint>
#include <typeindex>
#include <variant>
#include <utility>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>

#include <oneapi/tbb.h>

using namespace std;

class Event;

enum RequestID {
    ID_DEFAULT = 0x1,
    ID_READ = 0x2,
    ID_WRITE = 0x4,
    FLAG_REDO_CACHED_DATA = 0x10
};

enum class CallStatus { Degraded, Failed, Running, Finished, Stopped };

// Types passed to on_new_data for various event types

struct CallStarted {
    uint64_t procedure_id;
    type_index type;
    size_t obj_idx;
};

struct IoUringResult {
    uint64_t calling_id;
    int op;
    int res;
};

struct Wakeup {
    uint64_t task_id;
};

struct ChildTaskCompletion {
    uint64_t calling_id;
    uint64_t task_id;
    CallStatus status;
    int return_code;
};

struct Timeout {
    uint64_t timer_id;
};

using EventType = variant<CallStarted, Timeout, IoUringResult, Wakeup, ChildTaskCompletion>;

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

struct ConstructorCall {
    typedef function<shared_ptr<Event>()> Type;
    Type constructor;
    CallerInfo ci;
    TargetInfo ti;
};

struct FunctionCall {
    typedef function<CallResponse(shared_ptr<Event>, uint64_t id)> Type;
    Type call;
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

struct QueueContainer {
    oneapi::tbb::concurrent_queue<variant<ConstructorCall, FunctionCall, Delete, Data>> a;
};

// Types passed within a variant to the user data in IoUring calls

struct EventData {
    int op;
    uint64_t obj_id;
    uint64_t proc_id;
};

struct Timer {
    uint64_t timer_id;
};

struct TimerUpdate {
    bool remove;
};

struct IoUringAttached {
    std::variant<EventData, Timer, TimerUpdate> data;
};

enum OpHint {
    OP_HINT_NONE = 0,
    OP_HINT_FILESYSTEM = 1 << 0,
    OP_HINT_NETWORK = 1 << 1,
    OP_HINT_SERIAL = 1 << 2,
    OP_HINT_I2C = 1 << 3,
    OP_HINT_READ = 1 << 4,
    OP_HINT_WRITE = 1 << 5,
    OP_HINT_COMPUTE = 1 << 6,
    OP_HINT_WAIT = 1 << 7
};

struct ChildObjectInfo {
    int thread;
    unordered_set<uint64_t> procedures;
};

struct ObjectDataThreaded {
    shared_ptr<Event> ptr;
    // assoc_procs are associated procedures only for the current thread
    list<uint64_t> assoc_procs;
    // No hashing function available for pair
    set<pair<int, uint64_t>> parents;
    unordered_map<uint64_t, ChildObjectInfo> children;
};

// Thread-specific
struct CallDataThreaded {
    uint64_t assoc_obj;
    CallStatus status;
    string description;
    uint32_t op_hint;
    // thread, object, procedure
    tuple<int, uint64_t, uint64_t> back_notify;
    int return_code;
};

struct CallData {
    list<uint64_t> other_ids;
    CallStatus status;
    string description;
    uint32_t op_hint;
    list<uint64_t> parent_task_id;
    int return_code;
    Event* event;
    uint64_t thread_id;
};

struct GetDataInfo {
    bool valid;
};

struct TimerData {
    IoUringAttached* ptr;
    uint64_t obj_id;
    unique_ptr<__kernel_timespec> ts;
};
