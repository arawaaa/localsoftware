#pragma once

#include "../common/io_event.hpp"
#include "../common/io_uring_manager.hpp"
#include <liburing.h>

class BandwidthDataWriteEvent; // Forward declaration

class BandwidthDataTimerEvent : public IoEvent {
public:
    BandwidthDataTimerEvent(BandwidthDataWriteEvent* writer);

    void prepare_timer() {
        ts_.tv_sec = 1;
        ts_.tv_nsec = 0;
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_timeout, &ts_, 0, 0);
    }

    void on_new_data(int op, EventType event) override;

    std::string get_info() const override { return "BandwidthDataTimerEvent"; }

private:
    BandwidthDataWriteEvent* writer_;
    struct __kernel_timespec ts_;
};
