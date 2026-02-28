#pragma once

#include <vector>
#include <functional>
#include <tuple>
#include <utility>
#include <mutex>
#include <deque>
#include <optional>
#include <typeindex>
#include <queue>
#include <liburing.h>
#include <ranges>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "io_event.hpp"
#include "defs.hpp"

using namespace std;

class IoUringManager {
public:
    friend class IoEvent;

    static IoUringManager& getInstance() {
        static IoUringManager instance;
        return instance;
    }

    IoUringManager(const IoUringManager&) = delete;
    IoUringManager& operator=(const IoUringManager&) = delete;

    // Do not call queue any async functions in the constructor
    template <typename T, typename... Args>
    size_t initialize_dependent_event(IoEvent* parent, Args&&... args) {
        auto ev = make_shared<T>(std::forward<Args>(args)...);
        ev->uring_data_.outer_event = parent;
        for (auto [idx, shared_ptr] : views::enumerate(parent->uring_data_.events[type_index(typeid(T))])) {
            if (!shared_ptr) {
                shared_ptr = std::move(ev);
                ev->uring_data_.id = idx;
                return idx;
            }
        }
        parent->uring_data_.events[type_index(typeid(T))].emplace_back(std::move(ev));
        ev->uring_data_.id = parent->uring_data_.events[type_index(typeid(T))].size() - 1;
        return parent->uring_data_.events[type_index(typeid(T))].size() - 1;
    }

    template <typename T>
    auto get_data(IoEvent* parent, int idx, uint64_t id) {
        auto res = static_cast<T*>(parent->uring_data_.events[type_index(typeid(T))][idx].get())->get_data(id);
        using DataT = decltype(res.second);
        if (res.first.valid) {
            return optional<DataT>(std::move(res.second));
        }
        return optional<DataT>();
    }

    template <typename T, typename Method, typename... Args>
    pair<uint64_t, bool> call_dependent_function(IoEvent* parent, int idx, Method method, Args&&... args) {
        uint64_t id = next_id_++;
        uint64_t old_running_id = running_id_;
        running_id_ = id;
        
        T* ev = static_cast<T*>(parent->uring_data_.events[type_index(typeid(T))][idx].get());
        CallResponse resp = (ev->*method)(id, std::forward<Args>(args)...);
        
        CallData& data = call_map_[id];
        data.status = resp.success ? CallStatus::Running : CallStatus::Failed;
        data.description = std::move(resp.description);
        data.op_hint = resp.op_hint;
        data.parent_task_id = old_running_id;
        data.event = ev;
        
        if (old_running_id != 0) {
            call_map_[old_running_id].event = parent;
        }
        
        running_id_ = old_running_id;
        return {id, resp.success};
    }

    void finalize_current_task(bool failed, int return_code) {
        if (running_id_ == 0) return;
        auto& data = call_map_[running_id_];
        data.status = failed ? CallStatus::Failed : CallStatus::Finished;
        data.return_code = return_code;
    }

    void run(struct io_uring* ring) {
        while (true) {
            // Process non-uring events
            while (auto non_uring = dequeue_non_uring_event()) {
                auto [op, res, ev, rid] = *non_uring;
                uint64_t old_rid = running_id_;
                running_id_ = rid; 
                ev->on_new_data(op, IoUringResult{res});
                running_id_ = old_rid;
            }

            // Ensure pending submissions are sent
            submit_events(ring);

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
                    
                    uint64_t old_rid = running_id_;
                    running_id_ = rid;
                    ev->on_new_data(op, IoUringResult{(*ptr)->res});
                    
                    if (rid != 0 && (call_map_[rid].status == CallStatus::Finished || call_map_[rid].status == CallStatus::Failed)) {
                        propagation_queue_.push(rid);
                    }
                    
                    process_propagation_queue();
                    running_id_ = old_rid;
                    
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

    void consume_event(uint64_t taskid) {
        call_map_.erase(taskid);
    }

    void add(int op, int res, IoEvent* ev) {
        lock_guard<mutex> lock(non_uring_mutex_);
        non_uring_events_.emplace_back(op | RequestID::FLAG_REDO_CACHED_DATA, res, ev, running_id_);
    }

    optional<tuple<int, int, IoEvent*, uint64_t>> dequeue_non_uring_event() {
        lock_guard<mutex> lock(non_uring_mutex_);
        if (non_uring_events_.empty()) {
            return nullopt;
        }
        auto item = non_uring_events_.front();
        non_uring_events_.pop_front();
        return item;
    }

    void submit_events(struct io_uring* ring) {
        if (pending_events_.empty()) return;
        for (auto& item : pending_events_) {
            IoEvent* ev = get<0>(item);
            int op = get<1>(item);
            uint64_t rid = get<2>(item);
            auto& func = get<3>(item);
            
            struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
            if (sqe) {
                func(sqe);
                EventData* data = new EventData{op, rid, ev};
                io_uring_sqe_set_data(sqe, data);
            }
        }
        pending_events_.clear();
        io_uring_submit(ring);
    }

    SSL_CTX* get_tls_ctx() {
        return tls_ctx_;
    }

private:
    void process_propagation_queue() {
        while (!propagation_queue_.empty()) {
            uint64_t finished_id = propagation_queue_.front();
            propagation_queue_.pop();
            
            auto it = call_map_.find(finished_id);
            if (it == call_map_.end()) continue;
            
            uint64_t parent_id = it->second.parent_task_id;
            if (parent_id == 0) continue;
            
            auto& parent_entry = call_map_[parent_id];
            if (parent_entry.event) {
                uint64_t old_rid = running_id_;
                running_id_ = parent_id;
                
                ChildTaskCompletion comp = {finished_id, it->second.status, it->second.return_code};
                parent_entry.event->on_new_data(ID_DEFAULT, comp);
                
                if (parent_entry.status == CallStatus::Finished || parent_entry.status == CallStatus::Failed) {
                    propagation_queue_.push(parent_id);
                }
                
                running_id_ = old_rid;
            }
        }
    }

    IoUringManager() {
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

    ~IoUringManager() {
        SSL_CTX_free(tls_ctx_);
    }

    SSL_CTX* tls_ctx_;
    vector<tuple<IoEvent*, int, uint64_t, function<void(struct io_uring_sqe*)>>> pending_events_;
    mutex non_uring_mutex_;
    deque<tuple<int, int, IoEvent*, uint64_t>> non_uring_events_;
    uint64_t next_id_ = 1;
    uint64_t running_id_ = 0;
    map<uint64_t, CallData> call_map_;
    queue<uint64_t> propagation_queue_;
};
