#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <typeindex>
#include <liburing.h>
#include <optional>

#include "file.h"
#include "defs.h"
#include "async_thread_shared.h"
#include "coro_types.h"

using namespace std;
extern thread_local shared_ptr<IoUringData> context;
extern thread_local uint64_t uring_id;

class ThreadData;

extern thread_local ThreadData* tdata;

/**
 * @brief Base class for all io_uring events.
 */
class Event {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    /**
     * For constructors: do not call any delegated class functions. They will not have a context
     * to run within and will not be run!
     */
    explicit Event(vector<shared_ptr<File>> file);

    Event(Event&) = delete;
    Event operator=(Event&) = delete;

    virtual void construct_with_global() { };

    Event();

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
    template <typename Obj, typename... Args, typename... FnArgs>
    uint64_t c(size_t idx, CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args);

    template <typename Obj, typename... Args, typename... FnArgs>
    uint64_t c(CallResponse(Obj::*fun)(uint64_t, Args...), FnArgs... args);

    // Overload for liburing calls
    template <typename... Args, typename... FnArgs>
    void c(int op, void(*liburing)(io_uring_sqe*, Args...), FnArgs... args);

    // co_await liburing calls
    template <typename... Args, typename... FnArgs>
    readiness_api_awaitable ca(int op, void(*liburing)(io_uring_sqe*, Args...), FnArgs... args);

    // TODO RAII handle, possibly with C++26 reflection
    // With functions that return "Promise" objects that lambdas can be attached to
    template <typename Obj, typename... Args>
    size_t i(Args... args);

    template <typename Obj, typename... Args>
    size_t i_s(Args... args);

    template <typename Obj>
    void d(size_t idx);

    void attach(uint64_t id);

    uint64_t timer(__kernel_timespec ts);

    void cancel_timer(uint64_t timerid);

    // Directly accesses the function in target object. No synchronization - dangerous
    // Remove this and replace with promise objects
    template <typename R, typename Obj, typename... Args>
    optional<R> direct_access(size_t idx, R(Obj::*fun)(Args...), Args... args);

    void map_event_data(EventType& event);

    shared_ptr<IoUringData> uring_data_;

    template <typename T>
    list<T>& get_queued();

    void clear();

    void global_to_local(uint64_t global, uint64_t local);

    void global_to_local_tim(uint64_t global, uint64_t local);

    uint64_t translate_proc_local(uint64_t local);

    uint64_t translate_proc_global(uint64_t global);

    uint64_t translate_tim_local(uint64_t local);

    uint64_t translate_tim_global(uint64_t global);

    optional<EventInfo>& get_evinfo(type_index ti, size_t vi);
protected:
    vector<shared_ptr<File>> files_;

private:
    void event_init();

    template <typename Obj, typename... Args>
    size_t construct(bool shared, Args... args);

    list<EventQueuedConstruct> construct_;
    list<EventQueuedFunction> function_;
    list<EventQueuedUring> uring_;
    list<EventQueuedUringCo> uring_co_;
    list<EventQueuedTimer> timer_;
    list<EventQueuedDelete> delete_;
    list<EventQueuedAttach> attach_;
};

inline Event::~Event() = default;

#include "event.t.cpp"
