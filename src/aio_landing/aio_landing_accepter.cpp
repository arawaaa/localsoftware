#pragma once

#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>
#include <optional>

#include "aio_landing_server.cpp"
#include "common/defs.cpp"
#include "s40_client.cpp"

using namespace std;

class LandingAccept : public Event {
public:
    LandingAccept(vector<shared_ptr<File>> server_file, bool use_tls, const string& base_dir)
        : Event(server_file), use_tls_(use_tls), http_manager_(base_dir, use_tls ? "https" : "http")
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

    void construct_with_global() override {

    }

    CallResponse init(uint64_t taskid) {
        taskid_ = taskid;
        queue_accept(0);
        queue_accept(1);
        return {"HTTP accepter", true, nullopt, OP_HINT_NETWORK};
    }

    optional<pair<bool, int>> on_yield(EventType event) override {
        if (holds_alternative<ChildTaskCompletion>(event)) {
            std::cout << "Connection close" << std::endl;
            auto res = get<ChildTaskCompletion>(event);
            d<AioLandingHTTP>(indices[res.task_id]);
            indices.erase(res.task_id);
        } else {
            auto uri_res = get<IoUringResult>(event);

            if (uri_res.res >= 0) {
                char ip_str[INET6_ADDRSTRLEN];
                if (!uri_res.op)
                    inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
                else
                    inet_ntop(AF_INET6, &(client_addr6_.sin6_addr), ip_str, INET6_ADDRSTRLEN);

                cout << "[LANDING" << (use_tls_ ? " TLS" : "") << (uri_res.op ? " IPv6" : " IPv4")
                    << "] Accept with [" << ip_str << "]:"
                    << (uri_res.op ? client_addr6_.sin6_port : client_addr_.sin_port) << endl;

                auto client_file = vector<shared_ptr<File>>{make_shared<File>(uri_res.res)};

                int idx = i<AioLandingHTTP>(std::move(client_file), use_tls_, &http_manager_);
                uint64_t taskid = c(idx, &AioLandingHTTP::start);
                indices[taskid] = idx;
            }
            // Re-arm immediately
            client_addr_len_ = sizeof(sockaddr_in);
            client_addr6_len_ = sizeof(sockaddr_in6);
            queue_accept(uri_res.op);
        }
        return nullopt;
    }

    string get_info() const override { return "LandingAccept FD " + to_string(files_[0]->get()); }

private:
    void queue_accept(int op) {
        c(op, &io_uring_prep_accept, files_[op]->get(),
            op == 0 ? reinterpret_cast<struct sockaddr*>(&client_addr_)
                : reinterpret_cast<struct sockaddr*>(&client_addr6_),
            op == 0 ? &client_addr_len_
                : &client_addr6_len_, 0);
    }

    uint64_t taskid_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_ = sizeof(sockaddr_in);
    struct sockaddr_in6 client_addr6_{};
    socklen_t client_addr6_len_ = sizeof(sockaddr_in6);
    bool use_tls_;
    HTTPManager http_manager_;
    unordered_map<uint64_t, int> indices;
};
