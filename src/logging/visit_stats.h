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

    void get_pretty_stats(ostream& ret, string service = {}) noexcept;

    // Aligned to start of time bin
    uint64_t get_endpoint_stats(string service, string endpoint, time_t start, unsigned long end) noexcept;

    void add_access(string service, string endpoint);

    void register_service(string service);
};
