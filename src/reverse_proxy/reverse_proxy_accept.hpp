#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>

#include "common/io_event.hpp"
#include "common/io_uring_manager.hpp"
#include "common/http_manager.hpp"
#include "reverse_proxy_server.hpp"

using namespace std;

class OpenClawAccept : public IoEvent {
public:
    // Takes ownership of the listening server socket file descriptor
    OpenClawAccept(vector<shared_ptr<File>> server_file, const string& base_dir)
        : IoEvent(server_file), http_manager_(base_dir) {
        client_addr_len_ = sizeof(client_addr_);
    }

    CallResponse prepare_accept(uint64_t) {
        queue_accept();
        return {"Reverse Proxy accept", true, 0};
    }

    void on_new_data(int, EventType event) override {
        if (holds_alternative<ChildTaskCompletion>(event)) {
            std::cout << "Connection close" << std::endl;
            auto res = get<ChildTaskCompletion>(event);
            IoUringManager::getInstance().free_child_event_for_taskid<ReverseProxyServer>(this, res.task_id);
            IoUringManager::getInstance().consume_event(res.task_id);
        } else {
            int res = get<IoUringResult>(event).res;
            if (res >= 0) {
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
                cout << "[Reverse Proxy TLS] Accept with " << ip_str << ":" << client_addr_.sin_port << endl;
                auto client_file = vector<shared_ptr<File>>{make_shared<File>(res)};
                int idx = IoUringManager::getInstance().initialize_dependent_event<ReverseProxyServer>(this, client_file, http_manager_, proxyto_, proxyport_);
                IoUringManager::getInstance().call_dependent_function<ReverseProxyServer>(
                    this,
                    idx,
                    &ReverseProxyServer::start
                );
            }
            // Re-arm immediately
            queue_accept();
        }
    }

    std::string get_info() const override {
        return "OpenClaw on FD " + std::to_string(files_[0]->get());
    }

private:
    void queue_accept() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_accept, files_[0]->get(),
                         reinterpret_cast<struct sockaddr*>(&client_addr_),
                         &client_addr_len_, 0);
    }

    HTTPManager http_manager_;
    string proxyto_;
    int proxyport_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
};
