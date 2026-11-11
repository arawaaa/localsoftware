#pragma once

#include "event.h"
#include "defs.h"
#include "http_manager.h"
#include "coro_pinned.h"

using namespace std;

class LandingAccept : public Event {
public:
    LandingAccept(shared_ptr<File> server_file, bool use_tls, const string& base_dir);

    pinned<void> start();

    optional<pair<bool, int>> on_yield(EventType event) override;

    string get_info() const override;

private:
    struct sockaddr_in6 client_addr_{};
    socklen_t client_addr_len_ = sizeof(sockaddr_in6);
    bool use_tls_;
    HTTPManager http_manager_;
    unordered_map<uint64_t, int> indices;
};
