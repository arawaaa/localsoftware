#pragma once

#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>

#include "aio_landing_server.cpp"
#include "common/defs.cpp"
#include "common/io_uring_manager.cpp"
#include "s40_client.cpp"

using namespace std;

class AioLandingAcceptEvent : public IoEvent {
public:
    AioLandingAcceptEvent(vector<shared_ptr<File>> server_file, bool use_tls, const string& base_dir)
        : IoEvent(server_file), use_tls_(use_tls), http_manager_(base_dir, use_tls ? "https" : "http")
    {
        client_addr_len_ = sizeof(client_addr_);

        auto keep_alive_headers = vector<pair<http::field, string>>{
            {http::field::content_type, "text/plain"},
            {http::field::connection, "keep-alive"}
        };

        http_manager_.add_endpoint("/temperature", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                to_string(S40Client::getInstance().get_temperature()), req.version());
        });

        http_manager_.add_endpoint("/humidity", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                to_string(S40Client::getInstance().get_humidity()), req.version());
        });

        http_manager_.add_endpoint("/setpoint", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                to_string(S40Client::getInstance().get_setpoint()), req.version());
        });

        http_manager_.add_endpoint("/mode", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                S40Client::getInstance().get_mode(), req.version());
        });

        http_manager_.add_endpoint("/fan", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                to_string(S40Client::getInstance().get_fan()), req.version());
        });
    }

    CallResponse prepare_accept(uint64_t taskid) {
        taskid_ = taskid;
        queue_accept(0);
        queue_accept(1);
        return {"HTTP accepter", true, OP_HINT_NETWORK};
    }

    void on_new_data(int op, EventType event) override {
        if (holds_alternative<ChildTaskCompletion>(event)) {
            std::cout << "Connection close" << std::endl;
            auto res = get<ChildTaskCompletion>(event);
            IoUringManager::getInstance().free_child_event_for_taskid<AioLandingHTTP>(this, res.task_id);
        } else {
            int res = get<IoUringResult>(event).res;
            if (res >= 0) {
                char ip_str[INET6_ADDRSTRLEN];
                if (!op)
                    inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
                else
                    inet_ntop(AF_INET6, &(client_addr6_.sin6_addr), ip_str, INET6_ADDRSTRLEN);
                cout << "[LANDING" << (use_tls_ ? " TLS" : "") << (op ? " IPv6" : " IPv4") << "] Accept with [" << ip_str << "]:" << (op ? client_addr6_.sin6_port : client_addr_.sin_port) << endl;
                auto client_file = vector<shared_ptr<File>>{make_shared<File>(res)};
                int idx = IoUringManager::getInstance().initialize_dependent_event<AioLandingHTTP>(this, std::move(client_file), use_tls_, http_manager_);
                IoUringManager::getInstance().call_dependent_function<AioLandingHTTP>(
                    this,
                    idx,
                    &AioLandingHTTP::start
                );
            }
            // Re-arm immediately
            queue_accept(op);
        }
    }

    string get_info() const override { return "AioLandingAcceptEvent FD " + to_string(files_[0]->get()); }

private:
    void queue_accept(int op) {
        if (op == 0) {
            IoUringManager::getInstance().cache_call(
                this,
                0,
                io_uring_prep_accept,
                files_[0]->get(),
                reinterpret_cast<struct sockaddr*>(&client_addr_),
                &client_addr_len_,
                0
            );
        } else if (op == 1) {
            IoUringManager::getInstance().cache_call(
                this,
                1,
                io_uring_prep_accept,
                files_[1]->get(),
                reinterpret_cast<struct sockaddr*>(&client_addr6_),
                &client_addr6_len_,
                0
            );
        }
    }

    uint64_t taskid_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
    struct sockaddr_in6 client_addr6_{};
    socklen_t client_addr6_len_;
    bool use_tls_;
    HTTPManager http_manager_;
};
