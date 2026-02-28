#pragma once

#include <string>
#include <memory>
#include <utility>
#include <map>
#include <vector>
#include <typeindex>
#include <liburing.h>

#include "file.hpp"
#include "defs.hpp"

class IoEvent;

struct IoUringData {
    int id;
    std::map<std::type_index, std::vector<std::unique_ptr<IoEvent>>> events;
    IoEvent* outer_event;
};

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

    virtual ~IoEvent();

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    virtual void on_new_data(int op, EventType event) = 0;

    virtual std::string get_info() const = 0;

    IoUringData uring_data_;

protected:
    std::unique_ptr<File> file_;
    int fd_;
};

inline IoEvent::~IoEvent() = default;
