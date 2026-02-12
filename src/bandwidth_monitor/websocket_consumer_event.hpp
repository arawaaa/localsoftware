#pragma once

#include "../common/io_event.hpp"
#include "../common/io_uring_manager.hpp"
#include <vector>
#include <string>

class WebSocketConsumerEvent : public IoEvent {
public:
    // Non-owning constructor: uses the FD without wrapping it in a File object
    WebSocketConsumerEvent(int client_fd) 
        : IoEvent(client_fd) {
        buffer_.resize(1024);
    }

    void prepare_consumer() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_recv, fd_, buffer_.data(), buffer_.size(), 0);
    }

    void post(int id, int res) override {
        if (res <= 0) {
            delete this;
            return;
        }
        // Consume and discard client data (Pings, etc.)
        prepare_consumer();
    }

    std::string get_info() const override { return "WS Consumer FD " + std::to_string(fd_); }

private:
    std::vector<unsigned char> buffer_;
};