#pragma once

#include <string>
#include <list>
#include <cstdint>
#include <typeindex>
#include <variant>
#include <utility>

#include <oneapi/tbb.h>

using namespace std;

class IoEvent;

enum RequestID {
    ID_DEFAULT = 0x1,
    ID_READ = 0x2,
    ID_WRITE = 0x4,
    FLAG_REDO_CACHED_DATA = 0x10
};

enum class CallStatus { Failed, Running, Finished, Stopped };

// Types passed to on_new_data for various event types

struct CallStarted {
    uint64_t procedure_id;
    type_index type;
    size_t obj_idx;
};

struct IoUringResult {
    uint64_t calling_id;
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
    uint32_t op_hint;
};

struct CallerInfo {
    int thread_id;
    size_t obj_idx;
    uint64_t obj_id;
};

struct ConstructorCall {
    function<shared_ptr<IoEvent>()> constructor;
    CallerInfo ci;
    type_index type;
    uint64_t object_id;
};

struct FunctionCall {
    function<CallResponse(shared_ptr<IoEvent>)> call;
    CallerInfo ci;
    type_index type;
    uint64_t object_id;
    uint64_t procedure_id;
};

struct ProcedureUpdate {
    enum Type {
        StartConfirm,
        Yield
    };
    Type type;
    uint64_t object_id;
    CallResponse resp;
};

struct Delete {
    type_index type;
    uint64_t object_id;
};

struct Data {
    type_index type;
    uint64_t object_id;
    EventType data;
};

struct QueueContainer {
    oneapi::tbb::concurrent_queue<variant<ConstructorCall, FunctionCall, Delete, Data>> a;
};

// Types passed within a variant to the user data in IoUring calls

struct EventData {
    int op;
    uint64_t running_id;
    IoEvent* event;
};

struct Timer {
    uint64_t timer_id;
    uint64_t running_id;
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

struct CallDataThreaded {
    uint64_t assoc_obj;
    CallStatus status;
    string description;
    uint32_t op_hint;
    list<uint64_t> parents;
    list<uint64_t> children;
    int return_code;
};

struct CallData {
    list<uint64_t> other_ids;
    CallStatus status;
    string description;
    uint32_t op_hint;
    list<uint64_t> parent_task_id;
    int return_code;
    IoEvent* event;
    uint64_t thread_id;
};

struct GetDataInfo {
    bool valid;
};
