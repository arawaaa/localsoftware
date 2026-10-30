#include "bandwidth_data_timer.h"

#include <sys/timerfd.h>
#include <liburing.h>

#include "defs.h"

BandwidthDataTimer::BandwidthDataTimer()
{
    files_.emplace_back(make_shared<File>(timerfd_create(CLOCK_MONOTONIC, 0)));
};

CallResponse BandwidthDataTimer::prepare_timer(uint64_t) {
    ts_.it_interval.tv_sec = 0;
    ts_.it_interval.tv_nsec = 0;
    ts_.it_value.tv_sec = 1;
    ts_.it_value.tv_nsec = 0;
    timerfd_settime(files_[0]->get(), 0, &ts_, nullptr);
    c(ID_DEFAULT, io_uring_prep_read, files_[0]->get(), &expirations_, sizeof(expirations_), 0);

    return {"BandwidthDataTimer timeout", true, nullopt, 0};
}

optional<pair<bool, int>> BandwidthDataTimer::on_yield(EventType) {
    return pair{false, 1};
};

std::string BandwidthDataTimer::get_info() const {
    return "BandwidthDataTimer";
}
