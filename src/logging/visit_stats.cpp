#pragma once

#include <chrono>
#include <unordered_map>
#include <vector>
#include <utility>
#include <string>
#include <cstdint>
#include <list>
#include <mutex>

using namespace std;

class VisitStats {
    vector<pair<string, unordered_map<string, list<pair<time_t, uint64_t>>>>> visits_;
    mutex access;
    const time_t delta = 240;

    VisitStats() {}
public:
    VisitStats operator=(VisitStats&) = delete;
    VisitStats(VisitStats&) = delete;
    VisitStats(VisitStats&&) = delete;

    static VisitStats& getInstance() {
        static VisitStats stats;
        return stats;
    }

    void get_pretty_stats(ostream& ret, string service = {}) noexcept {
        lock_guard u(access);
        for (auto& visits_for_service : visits_) {
            if (service.empty() || visits_for_service.first == service) {
                ret << "Service: " << visits_for_service.first << endl;
                for (auto& [endpoint, list] : visits_for_service.second) {
                    ret << "Accesses for: " << endpoint << endl;
                    for (auto& [time, count] : list) {
                        ret << "\tTime bin: " << ctime(&time) << "\tCount: " << count << endl;
                    }
                }
            }
        }
    }

    // Aligned to start of time bin
    uint64_t get_endpoint_stats(string service, string endpoint, time_t start, unsigned long end) noexcept {
        lock_guard u(access);
        for (auto& visits_for_service : visits_) {
            if (visits_for_service.first == service) {
                if (auto it = visits_for_service.second.find(endpoint); it != visits_for_service.second.end()) {
                    uint64_t total = 0;
                    for (auto& [time, count] : it->second) {
                        if (time >= start && time <= start + delta * (signed)end) {
                            total += count;
                        }
                    }
                    return total;
                }
            }
        }
        return 0;
    }

    void add_access(string service, string endpoint) {
        lock_guard u(access);
        for (auto& visits_for_service : visits_) {
            if (visits_for_service.first == service) {
                auto& list = visits_for_service.second[endpoint];
                // Yes, I should use CLOCK_MONOTONIC, but I want actual time
                if (list.empty()) {
                    auto curr_bin = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count() / delta * delta;
                    list.emplace_back(curr_bin, 1);
                } else {
                    auto&[time, count] = list.back();
                    auto curr_bin = chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count() / delta * delta;
                    if (curr_bin == time) {
                        count++;
                    } else {
                        list.emplace_back(curr_bin, 1);
                    }
                }
            }
        }
    }

    void register_service(string service) {
        visits_.emplace_back(service, unordered_map<string, list<pair<time_t, uint64_t>>> ());
    }
};
