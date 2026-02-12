#pragma once

#include "io_event.hpp"
#include <utility>

class InetSocketReadWriteEvent : public IoEvent {
public:
    using IoEvent::IoEvent; // Inherit constructors

    InetSocketReadWriteEvent(std::unique_ptr<File> file, size_t bytes_left) 
        : IoEvent(std::move(file)), bytes_left_(bytes_left) {}

    InetSocketReadWriteEvent(int fd, size_t bytes_left) 
        : IoEvent(fd), bytes_left_(bytes_left) {}

    std::pair<bool, int> abstract_event_success(int res) override {
        if (res <= 0) {
            return {false, res};
        }
        
        bytes_left_ -= res;
        
        return {true, res};
    }

protected:
    size_t bytes_left_ = 0;
};
