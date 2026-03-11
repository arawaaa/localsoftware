#pragma once

#include "common/defs.hpp"
#include "common/io_event.hpp"
#include "common/io_uring_manager.hpp"
#include <ctime>
#include <sys/timerfd.h>

#include <liburing.h>

class BandwidthDataTimerEvent : public IoEvent {
public:
    BandwidthDataTimerEvent()
    {
        files_.emplace_back(make_shared<File>(timerfd_create(CLOCK_MONOTONIC, 0)));
    };

    CallResponse prepare_timer(uint64_t) {
        ts_.it_interval.tv_sec = 0;
        ts_.it_interval.tv_nsec = 0;
        ts_.it_value.tv_sec = 1;
        ts_.it_value.tv_nsec = 0;
        timerfd_settime(files_[0]->get(), 0, &ts_, nullptr);
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_read, files_[0]->get(), &expirations_, sizeof(expirations_), 0);

        return {"BandwidthDataTimer timeout", true, 0};
    }

    void on_new_data(int, EventType ev) override {
        auto res = std::get<IoUringResult>(ev);
        IoUringManager::getInstance().finalize_current_task(false, 1);
    };

    std::string get_info() const override { return "BandwidthDataTimerEvent"; }

private:
    uint64_t expirations_;
    struct itimerspec ts_;
};
