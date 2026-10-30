#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <typeindex>
#include <liburing.h>

#include <oneapi/tbb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "event.h"
#include "defs.h"
#include "thread_data.h"
#include "async_thread_shared.h"

using namespace std;

class AsyncHandler {
public:
    friend class Event;
    friend class ThreadData;

    static AsyncHandler& self(int ts = 1) {
        static AsyncHandler instance(ts);
        return instance;
    }

    void start();

    SSL_CTX* get_tls_ctx();

    /**
     * @brief Initializes the event, sends it to a thread
     * The root event class MUST define init(uint64_t)
     */
    template <typename Obj, typename... Args>
    void initialize_root_event(Args&&... args);

private:
    uint64_t get_proc_id();

    uint64_t get_obj_id();

    AsyncHandler(int ts);

    ~AsyncHandler();

    SSL_CTX* tls_ctx_;

    IdBlocks blocks;

    uint64_t r_proc_id_base, r_proc_id_curr; // Upper bound of block is id_base + 1000
    uint64_t r_obj_id_base, r_obj_id_curr;
    bool r_o_init = false, r_p_init = false;
    int pt_idx = 0;

    /** For operations impacting the size, i.e. auto-scaling, we will
     * halt all threads with a barrier, then will increase the size.
     * Size decreases are handled by the threads alone, and only if they
     * are the last element in the thread. Threads choose to destruct
     * if they are inactive.
     */
    mutex all_halt;
    vector<PerThread> per_thread_data;

    vector<ThreadData> private_per_thread_data;

    map<type_index, vector<shared_ptr<Event>>> root_events_;
};

#include "async_handler.t.cpp"

