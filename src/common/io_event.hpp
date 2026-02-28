#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <typeindex>
#include <liburing.h>

#include "file.hpp"
#include "defs.hpp"

using namespace std;

class IoEvent;

struct IoUringData {
    int id;
    map<type_index, vector<shared_ptr<IoEvent>>> events;
    IoEvent* outer_event;
};

/**
 * @brief Base class for all io_uring events.
 */
class IoEvent {
public:
    explicit IoEvent(shared_ptr<File> file) : file_(file) {}

    IoEvent() {}

    virtual ~IoEvent();

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    virtual void on_new_data(int op, EventType event) = 0;

    virtual string get_info() const = 0;

    IoUringData uring_data_;

protected:
    shared_ptr<File> file_ = nullptr;
};

inline IoEvent::~IoEvent() = default;
