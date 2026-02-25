#pragma once

#include <string>
#include <map>
#include <typeindex>

#include <set>
#include <cstdint>
#include <variant>

class IoEvent;

enum RequestID {
    ID_DEFAULT = 0x1,
    ID_READ = 0x2,
    ID_WRITE = 0x4,
    FLAG_REDO_CACHED_DATA = 0x10
};

enum class CallStatus { Failed, Running, Finished, Stopped };

struct IoUringResult {
    int res;
};

struct Wakeup {
    uint64_t task_id;
};

struct ChildTaskCompletion {
    uint64_t task_id;
    CallStatus status;
    int return_code;
};

using EventType = std::variant<IoUringResult, Wakeup, ChildTaskCompletion>;

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

struct CallResponse {
    std::string description;
    bool success;
    uint32_t op_hint;
};

struct CallData {
    std::set<uint64_t> other_ids;
    CallStatus status;
    std::string description;
    uint32_t op_hint;
    uint64_t parent_task_id;
    int return_code;
    IoEvent* event;
};

struct EventData {
    int op;
    uint64_t running_id;
    IoEvent* event;
};

struct GetDataInfo {
    bool valid;
};
