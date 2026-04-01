#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>
#include <format>

#include "common/io_event.cpp"
#include "common/io_uring_manager.cpp"
#include "common/http_manager.cpp"
#include "reverse_proxy_server.cpp"
#include "reverse_proxy_manager.cpp"

using namespace std;
constexpr string password = "arnavopenclaw";

class ReverseProxyAccept : public IoEvent {
public:
    // Takes ownership of the listening server socket file descriptor
    ReverseProxyAccept(vector<shared_ptr<File>> server_file, const string& base_dir)
        : IoEvent(server_file), http_manager_(base_dir) \
    {
        auto keep_alive_headers = vector<pair<http::field, string>>{
            {http::field::content_type, "text/plain"},
            {http::field::connection, "keep-alive"}
        };

        http_manager_.add_endpoint("/login", [this, keep_alive_headers](const auto& req) mutable {
            if (req.body() != proxy_manager_.password) {
                return HTTPManager::prepare_response(keep_alive_headers, http::status::unauthorized, "");
            } else {
                auto tok = proxy_manager_.create_new_token();
                keep_alive_headers.push_back({http::field::set_cookie, format("token={}; HttpOnly; Secure; SameSite=lax", tok)});
                return HTTPManager::prepare_response(keep_alive_headers, http::status::ok, proxy_manager_.redirect_url);
            }
        }, http::verb::post);

        ifstream ifs{"/etc/wlan_monitor/proxy.config"};
        string password;
        string redirect_url;
        string peer;
        string ports; int port;
        if (!getline(ifs, password) || password.empty()) {
            throw runtime_error{"Password field not present in config"};
        }
        if (!getline(ifs, redirect_url) || redirect_url.empty()) {
            throw runtime_error{"Redirect url not present"};
        }
        if (!getline(ifs, peer) || peer.empty()) {
            throw runtime_error{"Peer URL not present"};
        }
        if (!getline(ifs, ports)) {
            throw runtime_error{"Port not present"};
        }
        port = stoi(ports);

        proxy_manager_.password = password;
        proxy_manager_.redirect_url = redirect_url;
        proxy_manager_.peer_addr = peer;
        proxy_manager_.port = port;

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
        } else {
            int res = get<IoUringResult>(event).res;
            if (res >= 0) {
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
                cout << "[Reverse Proxy TLS] Accept with [" << ip_str << "]:" << client_addr_.sin_port << endl;
                auto client_file = vector<shared_ptr<File>>{make_shared<File>(res)};
                int idx = IoUringManager::getInstance().initialize_dependent_event<ReverseProxyServer>(this, client_file, http_manager_, proxy_manager_);
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
    ReverseProxyManager proxy_manager_;
    string proxyto_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
};
