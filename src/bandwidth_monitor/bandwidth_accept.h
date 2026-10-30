#pragma once

#include <sys/socket.h>
#include <netinet/in.h>

#include "event.h"
#include "http_manager.h"

using namespace std;

class BandwidthMonitorAccept : public Event {
public:
    // Takes ownership of the listening server socket file descriptor
    BandwidthMonitorAccept(vector<shared_ptr<File>> server_file, bool enable_tls, const string& base_dir);

    CallResponse init(uint64_t);

    optional<pair<bool, int>> on_yield(EventType event) override;

    std::string get_info() const override;

private:
    void queue_accept(int op);

    HTTPManager http_manager_;
    bool enable_tls_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
    struct sockaddr_in6 client_addr6_{};
    socklen_t client_addr6_len_;
    unordered_map<uint64_t, int> indices_;
};
