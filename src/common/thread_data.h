#pragma once

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

    uint64_t proc_id_base, proc_id_curr; // Upper bound of block is id_base + 1000
    uint64_t obj_id_base, obj_id_curr;
    uint64_t tim_id_base, tim_id_curr;
    bool o_init = false, p_init = false, t_init = false;

    vector<PerThread>& per_thread_data;
    IdBlocks& blocks;

public:
    ThreadData(vector<PerThread>& data, IdBlocks& blocks, int thread);

    void run();

    uint64_t get_proc_id();

    uint64_t get_obj_id();

    uint64_t get_tim_id();

    /*
     * Add new uring awaitable, return id handle
     */
    pair<uint64_t, list<Waiters::awaits>::iterator> add_co_uring(function<void(io_uring_sqe*)> sfunc, int* res);

private:
    /*
     * @brief Processes the queue
     * @returns Whether any message was processed at all
     */
    bool handle_dequeue();

    void process_uring();

    void construct_call(ConstructorCall& construct);

    void function_call(FunctionCall& func);

    void data_received(Data& on_data);

    int find_target_thread(unordered_set<int> threads = {});

    void process_queued(shared_ptr<Event> ptr, TargetInfo ti);

    void process_queued_construct(shared_ptr<Event> ptr, TargetInfo ti);

    void process_queued_function(shared_ptr<Event> ptr, TargetInfo ti);

    void process_queued_uring(shared_ptr<Event> ptr, TargetInfo ti);

    void process_queued_timers(shared_ptr<Event> ptr, TargetInfo ti);

    void process_queued_delete(shared_ptr<Event> ptr, TargetInfo ti);

    void process_queued_attach(shared_ptr<Event> ptr, TargetInfo ti);
};
