#pragma once

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "aio_landing_server.hpp"
#include "common/defs.hpp"
#include "common/io_uring_manager.hpp"
#include "s40_client.hpp"

class AioLandingAcceptEvent : public IoEvent {
public:
    AioLandingAcceptEvent(std::unique_ptr<File> server_file, bool use_tls, const std::string& base_dir)
        : IoEvent(std::move(server_file)), use_tls_(use_tls), http_manager_(base_dir)
    {
        client_addr_len_ = sizeof(client_addr_);

        auto keep_alive_headers = std::vector<std::pair<http::field, std::string>>{
            {http::field::content_type, "text/plain"},
            {http::field::connection, "keep-alive"}
        };

        http_manager_.add_endpoint("/temperature", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                std::to_string(S40Client::getInstance().get_temperature()), req.version());
        });

        http_manager_.add_endpoint("/humidity", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                std::to_string(S40Client::getInstance().get_humidity()), req.version());
        });

        http_manager_.add_endpoint("/setpoint", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                std::to_string(S40Client::getInstance().get_setpoint()), req.version());
        });

        http_manager_.add_endpoint("/mode", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                S40Client::getInstance().get_mode(), req.version());
        });

        http_manager_.add_endpoint("/fan", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok,
                std::to_string(S40Client::getInstance().get_fan()), req.version());
        });
    }

    void prepare_accept() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_accept, fd_,
                         reinterpret_cast<struct sockaddr*>(&client_addr_),
                         &client_addr_len_, 0);
    }

    void on_new_data(int op, EventType event) override {
        int res = std::get<IoUringResult>(event).res;
        if (res >= 0) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
            std::cout << "[LANDING" << (use_tls_ ? " TLS" : "") <<"] Accept with " << ip_str << ":" << client_addr_.sin_port << std::endl;
            auto client_file = std::make_unique<File>(res);
            int idx = IoUringManager::getInstance().initialize_dependent_event<AioLandingHTTP>(this, std::move(client_file), use_tls_, http_manager_);
            IoUringManager::getInstance().call_dependent_function<AioLandingHTTP>(
                this,
                idx,
                &AioLandingHTTP::start
            );
        }
        // Re-arm immediately
        prepare_accept();
    }

    std::string get_info() const override { return "AioLandingAcceptEvent FD " + std::to_string(fd_); }

private:
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
    bool use_tls_;
    HTTPManager http_manager_;
};
