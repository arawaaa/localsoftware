#include "event.h"

#include <cstdint>
#include <memory>
#include <map>
#include <vector>
#include <typeindex>
#include <unordered_map>
#include <liburing.h>
#include <optional>
#include <type_traits>

#include "file.h"
#include "defs.h"
#include "async_thread_shared.h"
#include "coro_types.h"

using namespace std;

Event::Event(vector<shared_ptr<File>> file) : files_(file) {
    event_init();
}

Event::Event() {
    event_init();
}

void Event::attach(uint64_t id) {
    attach_.emplace_back(EventQueuedAttach {
        .target_local_id = id
    });
}

uint64_t Event::timer(__kernel_timespec ts) {
    uint64_t id = uring_data_->local_timer_id++;

    timer_.emplace_back(EventQueuedTimer {
        .local_id = id,
        .time = ts
    });
    return id;
}

void Event::cancel_timer(uint64_t timerid) {
    timer_.emplace_back(EventQueuedTimer {
        .local_id = timerid,
        .time = {0, 0}
    });
}

void Event::map_event_data(EventType& event) {
    visit(overloaded {
        [this](ChildTaskCompletion& child) {
            child.task_id = uring_data_->global_proc_to_local_proc[child.task_id];
        },
        [this](Timeout& time) {
            time.timer_id = uring_data_->global_tim_to_local_tim[time.timer_id];
        },
        [this](CallStarted& call) {
            call.procedure_id = uring_data_->global_proc_to_local_proc[call.procedure_id];
        },
        [](auto&) {
        }
    }, event);
}

void Event::clear() {
    construct_.clear();
    function_.clear();
    uring_.clear();
    timer_.clear();
    delete_.clear();
    attach_.clear();
}

    void Event::global_to_local(uint64_t global, uint64_t local) {
        uring_data_->global_proc_to_local_proc[global] = local;
        uring_data_->local_proc_to_global_proc[local] = global;
    }

    void Event::global_to_local_tim(uint64_t global, uint64_t local) {
        uring_data_->global_tim_to_local_tim[global] = local;
        uring_data_->local_tim_to_global_tim[local] = global;
    }

uint64_t Event::translate_proc_local(uint64_t local) { if (uring_data_->local_proc_to_global_proc.contains(local)) return uring_data_->local_proc_to_global_proc.at(local); else return numeric_limits<uint64_t>::max(); }

uint64_t Event::translate_proc_global(uint64_t global) { if (uring_data_->global_proc_to_local_proc.contains(global)) return uring_data_->global_proc_to_local_proc.at(global); else return numeric_limits<uint64_t>::max(); }

uint64_t Event::translate_tim_local(uint64_t local) { if (uring_data_->local_tim_to_global_tim.contains(local)) return uring_data_->local_tim_to_global_tim.at(local); else return numeric_limits<uint64_t>::max(); }

uint64_t Event::translate_tim_global(uint64_t global) { if (uring_data_->global_tim_to_local_tim.contains(global)) return uring_data_->global_tim_to_local_tim.at(global); else return numeric_limits<uint64_t>::max(); }

optional<EventInfo>& Event::get_evinfo(type_index ti, size_t vi) {
    return uring_data_->sub_events[ti][vi];
}

void Event::event_init() {
    if (context) {
        uring_data_ = std::move(context);
    } else {
        uring_data_ = make_shared<IoUringData>();
    }
}
