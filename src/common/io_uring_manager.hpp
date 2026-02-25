#pragma once

#include <liburing.h>
#include <vector>
#include <functional>
#include <tuple>
#include <utility>
#include <mutex>
#include <deque>
#include <optional>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <typeindex>
#include <typeinfo>
#include "io_event.hpp"
#include "defs.hpp"

class IoUringManager {
public:
    friend class IoEvent;

    static IoUringManager& getInstance() {
        static IoUringManager instance;
        return instance;
    }

    IoUringManager(const IoUringManager&) = delete;
    IoUringManager& operator=(const IoUringManager&) = delete;

    template <typename T, typename... Args>
    void initialize_dependent_event(IoEvent* parent, Args&&... args) {
        auto ev = std::make_unique<T>(std::forward<Args>(args)...);
        ev->uring_data_.outer_event = parent;
        parent->uring_data_.events[std::type_index(typeid(T))] = std::move(ev);
    }

    template <typename T>
    auto get_data(IoEvent* parent, int id) {
        auto res = static_cast<T*>(parent->uring_data_.events[std::type_index(typeid(T))].get())->get_data(id);
        using DataT = decltype(res.second);
        if (res.first.valid) {
            return std::optional<DataT>(res.second);
        }
        return std::optional<DataT>();
    }

    template <typename T, typename Method, typename... Args>
    std::pair<uint64_t, bool> call_dependent_function(IoEvent* parent, Method method, Args&&... args) {
        uint64_t id = next_id_++;
        uint64_t old_running_id = running_id_;
        running_id_ = id;
        T* ev = static_cast<T*>(parent->uring_data_.events[std::type_index(typeid(T))].get());
        CallResponse resp = (ev->*method)(id, std::forward<Args>(args)...);
        
        CallData& data = call_map_[id];
        data.status = resp.success ? CallStatus::Running : CallStatus::Failed;
        data.description = std::move(resp.description);
        data.op_hint = resp.op_hint;
        
        running_id_ = old_running_id;
        return {id, resp.success};
    }

    void run(struct io_uring* ring) {
        while (true) {
            // Process non-uring events
            while (auto non_uring = dequeue_non_uring_event()) {
                auto [id, res, ev] = *non_uring;
                if (!(id & RequestID::FLAG_INTERNAL)) {
                    (void)ev->on_new_data(id, res);
                }
            }

            // Ensure pending submissions are sent
            submit_events(ring);
            io_uring_submit(ring); 

            struct io_uring_cqe *cqe[16] = {nullptr};
            struct __kernel_timespec ts ={
                .tv_sec = 10,
                .tv_nsec = 0
            };
            // Wait for completions
            if (io_uring_wait_cqes_min_timeout(ring, cqe, 16, &ts, 200, nullptr) < 0) {
                continue;
            }

            int i = 0;
            for (auto ptr = cqe; *ptr; ptr++) {
                if (!*ptr) break;
                EventData* data = reinterpret_cast<EventData*>(io_uring_cqe_get_data(*ptr));
                if (data) {
                    IoEvent* ev = data->event;
                    int op = data->op;
                    uint64_t rid = data->running_id;
                    if (!(op & RequestID::FLAG_INTERNAL)) {
                        uint64_t old_rid = running_id_;
                        running_id_ = rid;
                        (void)ev->on_new_data(op, (*ptr)->res);
                        running_id_ = old_rid;
                    }
                    delete data;
                }
                i++;
            }

            io_uring_cq_advance(ring, i);
        }
    }

    template <typename F, typename... Args>
    void cache_call(IoEvent* ev, int op, F&& func, Args&&... args) {
        pending_events_.emplace_back(ev, op, running_id_, [f = std::forward<F>(func), ...args = std::forward<Args>(args)](struct io_uring_sqe* sqe) mutable {
            f(sqe, args...);
        });
    }

    void add(int id, int res, IoEvent* ev) {
        std::lock_guard<std::mutex> lock(non_uring_mutex_);
        non_uring_events_.emplace_back(id | RequestID::FLAG_REDO_CACHED_DATA, res, ev);
    }

    std::optional<std::tuple<int, int, IoEvent*>> dequeue_non_uring_event() {
        std::lock_guard<std::mutex> lock(non_uring_mutex_);
        if (non_uring_events_.empty()) {
            return std::nullopt;
        }
        auto item = non_uring_events_.front();
        non_uring_events_.pop_front();
        return item;
    }

    void submit_events(struct io_uring* ring) {
        for (auto& item : pending_events_) {
            IoEvent* ev = std::get<0>(item);
            int op = std::get<1>(item);
            uint64_t rid = std::get<2>(item);
            auto& func = std::get<3>(item);
            
            struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
            if (sqe) {
                func(sqe);
                EventData* data = new EventData{op, rid, ev};
                io_uring_sqe_set_data(sqe, data);
            }
        }
        pending_events_.clear();
    }

    SSL_CTX* get_tls_ctx() {
        return tls_ctx_;
    }

private:
    IoUringManager() {
        tls_ctx_ = SSL_CTX_new(TLS_server_method());
        if (tls_ctx_ == NULL) {
            throw std::runtime_error{"Unable to create libssl context"};
        }

        if (!SSL_CTX_set_min_proto_version(tls_ctx_, TLS1_2_VERSION)) {
            SSL_CTX_free(tls_ctx_);
            throw std::runtime_error{"Unable to set minimum TLS version to 1.2"};
        }

        auto opts = SSL_OP_IGNORE_UNEXPECTED_EOF | SSL_OP_NO_RENEGOTIATION | SSL_OP_CIPHER_SERVER_PREFERENCE;
        SSL_CTX_set_options(tls_ctx_, opts);

        if (SSL_CTX_use_certificate_chain_file(tls_ctx_, "/etc/letsencrypt/live/arnavrawat.xyz/fullchain.pem") <= 0) {
            SSL_CTX_free(tls_ctx_);
            throw std::runtime_error{"Unable to load certificate chain"};
        }

        if (SSL_CTX_use_PrivateKey_file(tls_ctx_, "/etc/letsencrypt/live/arnavrawat.xyz/privkey.pem", SSL_FILETYPE_PEM) <= 0) {
            SSL_CTX_free(tls_ctx_);
            throw std::runtime_error{"Unable to load private key. Check for certificate / key mismatch."};
        }

        // No session resumption for now

        SSL_CTX_set_verify(tls_ctx_, SSL_VERIFY_NONE, NULL);
    };

    ~IoUringManager() {
        SSL_CTX_free(tls_ctx_);
    }

    SSL_CTX* tls_ctx_;
    std::vector<std::tuple<IoEvent*, int, uint64_t, std::function<void(struct io_uring_sqe*)>>> pending_events_;
    std::mutex non_uring_mutex_;
    std::deque<std::tuple<int, int, IoEvent*>> non_uring_events_;
    uint64_t next_id_ = 1;
    uint64_t running_id_ = 0;
    std::map<uint64_t, CallData> call_map_;
};
