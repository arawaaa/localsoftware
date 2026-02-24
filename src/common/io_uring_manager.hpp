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
    auto get_data(IoEvent* parent, int id) -> decltype(std::declval<T>().get_data(id)) {
        return static_cast<T*>(parent->uring_data_.events[std::type_index(typeid(T))].get())->get_data(id);
    }

    template <typename F, typename... Args>
    void cache_call(IoEvent* ev, int id, F&& func, Args&&... args) {
        pending_events_.emplace_back(ev, id, [f = std::forward<F>(func), ...args = std::forward<Args>(args)](struct io_uring_sqe* sqe) mutable {
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
            int id = std::get<1>(item);
            auto& func = std::get<2>(item);
            
            struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
            if (sqe) {
                func(sqe);
                EventData* data = new EventData{id, ev};
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
    std::vector<std::tuple<IoEvent*, int, std::function<void(struct io_uring_sqe*)>>> pending_events_;
    std::mutex non_uring_mutex_;
    std::deque<std::tuple<int, int, IoEvent*>> non_uring_events_;
};
