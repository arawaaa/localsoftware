#pragma once

#include "../common/io_event.hpp"
#include <vector>
#include <string>

class WebSocketConsumerEvent : public IoEvent {
public:
    // Non-owning constructor: uses the FD without wrapping it in a File object
    WebSocketConsumerEvent(int client_fd, struct io_uring* ring) 
        : IoEvent(client_fd), ring_(ring) {
        buffer_.resize(1024);
    }

    void run(struct io_uring_sqe* sqe) override {
        io_uring_prep_recv(sqe, fd_, buffer_.data(), buffer_.size(), 0);
    }

    void post(int res) override {
        if (res <= 0) {
            delete this;
            return;
        }
        // Consume and discard client data (Pings, etc.)
        this->on(ring_);
    }

    std::string get_info() const override { return "WS Consumer FD " + std::to_string(fd_); }

private:
    struct io_uring* ring_;
    std::vector<unsigned char> buffer_;
};