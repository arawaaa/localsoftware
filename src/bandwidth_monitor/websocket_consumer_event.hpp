#pragma once

#include "../common/io_event.hpp"
#include "../common/io_uring_manager.hpp"
#include <vector>
#include <string>

using namespace std;

class WebSocketConsumerEvent : public IoEvent {
public:
    // Non-owning constructor: uses the FD without wrapping it in a File object
    WebSocketConsumerEvent(vector<shared_ptr<File>> file)
        : IoEvent(file) {
        buffer_.resize(1024);
    }

    void prepare_consumer() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_recv, files_[0]->get(), buffer_.data(), buffer_.size(), 0);
    }

    void on_new_data(int, EventType event) override {
        int res = std::get<IoUringResult>(event).res;
        if (res <= 0) {
            delete this;
            return;
        }
        // Consume and discard client data (Pings, etc.)
        prepare_consumer();
    }

    std::string get_info() const override { return "WS Consumer FD " + std::to_string(files_[0]->get()); }

private:
    std::vector<unsigned char> buffer_;
};
