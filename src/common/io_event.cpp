#pragma once

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <typeindex>
#include <liburing.h>

#include "file.cpp"
#include "defs.cpp"

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
    explicit IoEvent(vector<shared_ptr<File>> file) : files_(file) {}

    IoEvent() {}

    virtual ~IoEvent();

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    virtual void on_new_data(int op, EventType event) = 0;

    virtual string get_info() const = 0;

    IoUringData uring_data_;

protected:
    vector<shared_ptr<File>> files_;
};

inline IoEvent::~IoEvent() = default;
