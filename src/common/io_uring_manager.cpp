#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <variant>
#include <vector>
#include <functional>
#include <tuple>
#include <utility>
#include <optional>
#include <typeindex>
#include <queue>
#include <liburing.h>
#include <ranges>
#include <functional>
#include <iostream>
#include <thread>

#include <oneapi/tbb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "io_event.cpp"
#include "defs.cpp"
#include "thread_data.cpp"
#include "async_thread_shared.cpp"

using namespace std;

class ThreadData;

class AsyncHandler {
public:
    friend class Event;
    friend class ThreadData;

    static AsyncHandler& self(int ts = 1) {
        static AsyncHandler instance(ts);
        return instance;
    }

    void start() {
        vector<thread> threads;
        for (size_t i = 0; i < per_thread_data.size(); i++) {
            private_per_thread_data.emplace_back(ThreadData(per_thread_data, blocks, i));
            threads.emplace_back(&ThreadData::run, &private_per_thread_data[i]);
        }
        for (auto &t : threads) {
            t.join();
        }
    }

    SSL_CTX* get_tls_ctx() {
        return tls_ctx_;
    }

    /**
     * @brief Initializes the event, sends it to a thread
     * The root event class MUST define init(uint64_t)
     */
    template <typename Obj, typename... Args>
    void initialize_root_event(Args&&... args) {
        auto f = [args...] mutable {
            return make_shared<Obj>(std::forward<Args>(args)...);
        };

        auto c = [](shared_ptr<Event> ptr, uint64_t id) -> CallResponse {
            return static_pointer_cast<Obj>(ptr)->init(id);
        };

        RootStart callinfo {
            .constructor = f,
            .init = c,
            .ti = TargetInfo {
                .obj_id = get_obj_id(),
                .proc_id = get_proc_id()
            }
        };

        per_thread_data[pt_idx].q.push(callinfo);
    }

private:
    uint64_t get_proc_id() {
        if (r_proc_id_curr == r_proc_id_base + 1000 || !r_p_init) {
            r_proc_id_base = r_proc_id_curr = 1000 * blocks.proc_block_++;
            r_p_init = true;
        }
        return r_proc_id_curr++;
    }

    uint64_t get_obj_id() {
        if (r_obj_id_curr == r_obj_id_base + 1000 || !r_o_init) {
            r_obj_id_base = r_obj_id_curr = 1000 * blocks.obj_block_++;
            r_o_init = true;
        }
        return r_obj_id_curr++;
    }

    AsyncHandler(int ts) : per_thread_data(ts) {
        for (int i = 0; i < ts; i++) {
            private_per_thread_data.emplace_back(per_thread_data, blocks, i);
        }

        tls_ctx_ = SSL_CTX_new(TLS_server_method());
        if (tls_ctx_ == NULL) {
            throw runtime_error{"Unable to create libssl context"};
        }

        if (!SSL_CTX_set_min_proto_version(tls_ctx_, TLS1_2_VERSION)) {
            SSL_CTX_free(tls_ctx_);
            throw runtime_error{"Unable to set minimum TLS version to 1.2"};
        }

        auto opts = SSL_OP_IGNORE_UNEXPECTED_EOF | SSL_OP_NO_RENEGOTIATION | SSL_OP_CIPHER_SERVER_PREFERENCE;
        SSL_CTX_set_options(tls_ctx_, opts);

        if (SSL_CTX_use_certificate_chain_file(tls_ctx_, "/etc/letsencrypt/live/arnavrawat.xyz/fullchain.pem") <= 0) {
            SSL_CTX_free(tls_ctx_);
            throw runtime_error{"Unable to load certificate chain"};
        }

        if (SSL_CTX_use_PrivateKey_file(tls_ctx_, "/etc/letsencrypt/live/arnavrawat.xyz/privkey.pem", SSL_FILETYPE_PEM) <= 0) {
            SSL_CTX_free(tls_ctx_);
            throw runtime_error{"Unable to load private key. Check for certificate / key mismatch."};
        }

        // No session resumption for now

        SSL_CTX_set_verify(tls_ctx_, SSL_VERIFY_NONE, NULL);
    };

    ~AsyncHandler() {
        SSL_CTX_free(tls_ctx_);
    }

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

