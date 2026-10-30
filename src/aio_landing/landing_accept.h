#pragma once

#include "event.h"
#include "defs.h"
#include "http_manager.h"

using namespace std;

class LandingAccept : public Event {
public:
    LandingAccept(vector<shared_ptr<File>> server_file, bool use_tls, const string& base_dir);

    CallResponse init(uint64_t taskid);

    optional<pair<bool, int>> on_yield(EventType event) override;

    string get_info() const override;

private:
    void queue_accept(int op);

    uint64_t taskid_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_ = sizeof(sockaddr_in);
    struct sockaddr_in6 client_addr6_{};
    socklen_t client_addr6_len_ = sizeof(sockaddr_in6);
    bool use_tls_;
    HTTPManager http_manager_;
    unordered_map<uint64_t, int> indices;
};
