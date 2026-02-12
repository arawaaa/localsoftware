#pragma once

#include <liburing.h>
#include <string>
#include <memory>
#include <utility>
#include "file.hpp"

/**
 * @brief Base class for all io_uring events.
 */
class IoEvent {
public:
    explicit IoEvent(std::unique_ptr<File> file) : file_(std::move(file)) {
        fd_ = file_ ? (*file_)() : -1;
    }
    
    // Support for events that share an FD (non-owning)
    IoEvent(int fd) : fd_(fd) {}

    virtual ~IoEvent() = default;

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    virtual void post(int id, int res) = 0;

    /**
     * @brief Checks if the event was successful and returns a success flag along with a result code.
     */
    virtual std::pair<bool, int> abstract_event_success(int id, int res) { return {true, res}; }

    virtual std::string get_info() const = 0;

protected:
    std::unique_ptr<File> file_;
    int fd_;
};
