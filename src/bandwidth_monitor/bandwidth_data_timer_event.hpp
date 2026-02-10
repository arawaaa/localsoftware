#pragma once

#include "../common/io_event.hpp"
#include <liburing.h>
#include <iostream>

class BandwidthDataWriteEvent; // Forward declaration

class BandwidthDataTimerEvent : public IoEvent {
public:
    BandwidthDataTimerEvent(BandwidthDataWriteEvent* writer, struct io_uring* ring);

    void run(struct io_uring_sqe* sqe) override {
        ts_.tv_sec = 1;
        ts_.tv_nsec = 0;
        io_uring_prep_timeout(sqe, &ts_, 0, 0);
    }

    void post(int res) override;

    std::string get_info() const override { return "BandwidthDataTimerEvent"; }

private:
    BandwidthDataWriteEvent* writer_;
    struct io_uring* ring_;
    struct __kernel_timespec ts_;
};
