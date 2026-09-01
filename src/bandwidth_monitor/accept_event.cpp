#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>

#include "common/io_event.cpp"
#include "common/io_uring_manager.cpp"
#include "bandwidth_monitoring_server.cpp"

using namespace std;

class BandwidthMonitorAcceptEvent : public Event {
public:
    // Takes ownership of the listening server socket file descriptor
    BandwidthMonitorAcceptEvent(vector<shared_ptr<File>> server_file, bool enable_tls, const string& base_dir)
        : Event(server_file), http_manager_(base_dir), enable_tls_(enable_tls) {
        client_addr_len_ = sizeof(client_addr_);
    }

    CallResponse init(uint64_t) {
        queue_accept(0);
        queue_accept(1);
        return {"Bandwidth Monitor accept", true, nullopt, OP_HINT_NETWORK};
    }

    optional<pair<bool, int>> on_yield(EventType event) override {
        if (holds_alternative<ChildTaskCompletion>(event)) {
            std::cout << "Connection close" << std::endl;
            auto res = get<ChildTaskCompletion>(event);
            d<BandwidthMonitoringServer>(indices_[res.task_id]);
        } else {
            IoUringResult& res = get<IoUringResult>(event);
            if (res.res >= 0) {
                char ip_str[INET6_ADDRSTRLEN];
                if (!res.op)
                    inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
                else
                    inet_ntop(AF_INET6, &(client_addr6_.sin6_addr), ip_str, INET6_ADDRSTRLEN);
                cout << "[MONITOR" << (enable_tls_ ? " TLS" : "") << (res.op ? " IPv6" : " IPv4") << "] Accept with [" << ip_str << "]:" << (res.op ? client_addr6_.sin6_port : client_addr_.sin_port) << endl;
                auto client_file = vector<shared_ptr<File>>{make_shared<File>(res.res)};
                int idx = i<BandwidthMonitoringServer>(client_file, enable_tls_, &http_manager_);
                uint64_t taskid = c(idx, &BandwidthMonitoringServer::start);
                indices_[taskid] = idx;
            }
            // Re-arm immediately
            queue_accept(res.op);
        }
        return nullopt;
    }

    std::string get_info() const override {
        return "BandwidthMonitorAcceptEvent on FD " + std::to_string(files_[0]->get());
    }

private:
    void queue_accept(int op) {
        c(op, &io_uring_prep_accept, files_[op]->get(),
            op == 0 ? reinterpret_cast<struct sockaddr*>(&client_addr_)
                : reinterpret_cast<struct sockaddr*>(&client_addr6_),
            op == 0 ? &client_addr_len_
                : &client_addr6_len_, 0);
    }

    HTTPManager http_manager_;
    bool enable_tls_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
    struct sockaddr_in6 client_addr6_{};
    socklen_t client_addr6_len_;
    unordered_map<uint64_t, int> indices_;
};
