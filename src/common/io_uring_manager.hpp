#pragma once

#include <liburing.h>
#include <vector>
#include <functional>
#include <tuple>
#include <utility>
#include "io_event.hpp"
#include "defs.hpp"

class IoUringManager {
public:
    static IoUringManager& getInstance() {
        static IoUringManager instance;
        return instance;
    }

    IoUringManager(const IoUringManager&) = delete;
    IoUringManager& operator=(const IoUringManager&) = delete;

    template <typename F, typename... Args>
    void cache_call(IoEvent* ev, int id, F&& func, Args&&... args) {
        pending_events_.emplace_back(ev, id, [f = std::forward<F>(func), ...args = std::forward<Args>(args)](struct io_uring_sqe* sqe) mutable {
            f(sqe, args...);
        });
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

private:
    IoUringManager() = default;
    std::vector<std::tuple<IoEvent*, int, std::function<void(struct io_uring_sqe*)>>> pending_events_;
};
