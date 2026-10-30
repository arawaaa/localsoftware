#pragma once

#include <sys/timerfd.h>

#include "event.h"

class BandwidthDataTimer : public Event {
public:
    BandwidthDataTimer();

    CallResponse prepare_timer(uint64_t);

    optional<pair<bool, int>> on_yield(EventType) override;

    std::string get_info() const override;

private:
    uint64_t expirations_;
    struct itimerspec ts_;
};
