#pragma once

#include <coroutine>
#include <cstdint>
#include <iostream>
#include <liburing.h>
#include <limits>
#include <memory>
#include <malloc.h>

#include "defs.h"
#include "async_thread_shared.h"
#include "event.h"
#include "alloc_wrapper.h"

using namespace std;
class ThreadData;

extern thread_local shared_ptr<IoUringData> context;
extern thread_local AllocatorWrapper alloc;
extern thread_local uint64_t uring_id;
extern thread_local ThreadData* tdata;

/*
 * Class for managing a ring for each thread, triggering awaiters and on_yield functions.
 * Needs logic for adding more rings for auto-downscaling
 */
class ThreadData {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };

    int thread;

    io_uring ring;
    io_uring_cqe *cqe = nullptr;

    typedef unordered_map<uint64_t, CallDataThreaded> CallMap;
    typedef unordered_map<uint64_t, ObjectDataThreaded> ObjectMap;
    typedef unordered_map<uint64_t, TimerData> TimerMap;

    // TODO clean this up as well when we delete objects
    CallMap proc_to_dat;
    ObjectMap obj_info;
    TimerMap timers;
    map<uint64_t, list<pair<uint64_t, uint64_t>>> also_notify;
    unordered_map<uint64_t, Waiters> readiness_waiters;
    deque<tuple<function<void(io_uring_sqe*)>, uint64_t>> sqes;

    // HACK Need better method for initiating root events
    vector<pair<shared_ptr<task>, shared_ptr<Event>>> root_tasks;

    uint64_t proc_id_base, proc_id_curr; // Upper bound of block is id_base + 1000
    uint64_t obj_id_base, obj_id_curr;
    uint64_t tim_id_base, tim_id_curr;
    bool o_init = false, p_init = false, t_init = false;

    // HACK interop
    uint64_t co_obj_id, co_proc_id;

    vector<PerThread>& per_thread_data;
    IdBlocks& blocks;

public:
    ThreadData(vector<PerThread>& data, IdBlocks& blocks, int thread);

    ThreadData(ThreadData&& other) = default;

    void run();

    uint64_t get_proc_id();

    uint64_t get_obj_id();

    uint64_t get_tim_id();

    // Readiness awaitable notification functions

    /*
     * Add new uring awaitable, return id handle
     */
    uint64_t co_register(function<void(io_uring_sqe*)> sfunc, coroutine_handle<> handle);

    void co_deregister(uint64_t id);

    int co_result(uint64_t id);

private:
    /*
     * @brief Processes the queue
     * @returns Whether any message was processed at all
     */
    bool handle_dequeue();

    void queue_uring();

    void process_uring();

    int find_target_thread(unordered_set<int> threads = {});
};
