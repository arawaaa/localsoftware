#pragma once

#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>

#include "aio_landing_server.hpp"
#include "common/defs.hpp"
#include "common/io_uring_manager.hpp"
#include "s40_client.hpp"

using namespace std;

class AioLandingAcceptEvent : public IoEvent {
public:
    AioLandingAcceptEvent(shared_ptr<File> server_file, bool use_tls, const string& base_dir)
        : IoEvent(server_file), use_tls_(use_tls), http_manager_(base_dir)
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
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_accept, file_->get(),
                         reinterpret_cast<struct sockaddr*>(&client_addr_),
                         &client_addr_len_, 0);
        return {"HTTP accepter", true, OP_HINT_NETWORK};
    }

    void on_new_data(int, EventType event) override {
        if (holds_alternative<ChildTaskCompletion>(event)) {
            std::cout << "Connection close" << std::endl;
            auto res = get<ChildTaskCompletion>(event);
            IoUringManager::getInstance().free_child_event_for_taskid<AioLandingHTTP>(this, res.task_id);
            IoUringManager::getInstance().consume_event(res.task_id);
        } else {
            int res = get<IoUringResult>(event).res;
            if (res >= 0) {
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
                cout << "[LANDING" << (use_tls_ ? " TLS" : "") <<"] Accept with " << ip_str << ":" << client_addr_.sin_port << endl;
                auto client_file = make_shared<File>(res);
                int idx = IoUringManager::getInstance().initialize_dependent_event<AioLandingHTTP>(this, std::move(client_file), use_tls_, http_manager_);
                IoUringManager::getInstance().call_dependent_function<AioLandingHTTP>(
                    this,
                    idx,
                    &AioLandingHTTP::start
                );
            }
            // Re-arm immediately
            prepare_accept(taskid_);
        }
    }

    string get_info() const override { return "AioLandingAcceptEvent FD " + to_string(file_->get()); }

private:
    uint64_t taskid_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
    bool use_tls_;
    HTTPManager http_manager_;
};
