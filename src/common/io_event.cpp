#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <unordered_set>
#include <typeindex>
#include <unordered_map>
#include <liburing.h>
#include <optional>

#include "file.cpp"
#include "defs.cpp"

using namespace std;

class ThreadData;

/**
 * @brief Base class for all io_uring events.
 */
class Event {
public:
    struct EventInfo {
        uint64_t object_id;
        unordered_set<int> thread_id;
        weak_ptr<Event> event;
    };

    struct IoUringData {
        int id;
        ThreadData* thread_data;
        unordered_map<type_index, vector<optional<EventInfo>>> sub_events;
        map<uint64_t, type_index> awaiting_resolve;
        Event* outer_event;
    };

    /**
     * For constructors: do not call any delegated class functions. They will not have a context
     * to run within and will not be run!
     */
    explicit Event(vector<shared_ptr<File>> file) : files_(file) {}

    Event() {}

    virtual ~Event();

    /**
     * @brief Handle data yielded from a delegated operation
     * @returns If task complete, pair<failure, ret_code>, if need to continue, nullopt
     */
    virtual optional<pair<bool, int>> on_yield(EventType event) = 0;

    /**
     * @brief Get string description for class
     */
    virtual string get_info() const = 0;

    /**
     * @brief Whether the class can be run on multiple threads
     */
    constexpr virtual bool is_thread_safe() {
        return false;
    }

    /**
     * @brief Called on successful start of a function call
     *
     * Do not start any non-direct functions from this function
     */
    virtual void start_response(uint64_t, CallResponse) { };

    /**
     * @brief Call a function in a subclass, automatically choosing the thread,
     * minimizing excessive minimizing out-of-current-thread calls
     *
     * Definition in AsyncHandler: depends on ThreadData
     */
    template <typename Obj, typename... Args>
    uint64_t c(size_t idx, CallResponse(Obj::*fun)(uint64_t, Args...), Args... args);

    template <typename Obj, typename... Args>
    uint64_t c(CallResponse(Obj::*fun)(uint64_t, Args...), Args... args);

    // Overload for liburing calls
    template <typename... Args>
    void c(int op, void(*liburing)(io_uring_sqe*, Args...), Args... args);

    // Todo return RAII handle for created event instead of forcing clients to keep track of index
    template <typename Obj, typename... Args>
    size_t i(Args... args);

    template <typename Obj>
    void d(size_t idx);

    uint64_t timer(__kernel_timespec ts);

    // Directly accesses the function in target object. No synchronization - dangerous
    template <typename R, typename Obj, typename... Args>
    optional<R> direct_access(size_t idx, R(Obj::*fun)(Args...), Args... args) {
        shared_ptr<Obj> ptr = static_pointer_cast<Obj>(
            uring_data_.sub_events.at(type_index(typeid(Obj))).at(idx).value().event.lock());
        if (!ptr) return nullopt;
        return (ptr.get()->*fun)(args...);
    }

    IoUringData uring_data_;

protected:
    vector<shared_ptr<File>> files_;
};

inline Event::~Event() = default;

#include "io_uring_manager.cpp"
