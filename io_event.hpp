#pragma once

#include <liburing.h>
#include <string>
#include <memory>
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
     * @brief Prepare the submission queue entry (SQE).
     */
    virtual void run(struct io_uring_sqe* sqe) = 0;

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    virtual void post(int res) = 0;

    /**
     * @brief Utility to place this event onto the ring.
     */
    void on(struct io_uring* ring) {
        struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
        if (sqe) {
            this->run(sqe);
            io_uring_sqe_set_data(sqe, this);
        }
    }

    /**
     * @brief Link a subsequent event to this one using IOSQE_IO_LINK.
     */
    void link(IoEvent* next) {
        linked_event_ = next;
    }

    virtual std::string get_info() const = 0;

protected:
    std::unique_ptr<File> file_;
    int fd_;
    IoEvent* linked_event_ = nullptr;
};
