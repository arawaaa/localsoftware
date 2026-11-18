#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>
#include <optional>

#include "landing_accept.h"
#include "landing_server.h"
#include "readwrite_http.h"
#include "s40_client.h"

using namespace std;

LandingAccept::LandingAccept(shared_ptr<File> server_file, bool use_tls, const string& base_dir)
    : Event({server_file}), use_tls_(use_tls), http_manager_(base_dir, use_tls ? "https" : "http")
{
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

pinned<void> LandingAccept::start() {
    while (true) {
        client_addr_len_ = sizeof(sockaddr_in6);
        int connection = co_await c(io_uring_prep_accept, files_[0]->get(), reinterpret_cast<struct sockaddr*>(&client_addr_),
                                    &client_addr_len_, 0);

        if (connection <= 0) {
            // TODO Fork-join container in localsoftware.cpp, with per-result handling decisions
            // can be cancel-all or wait for remaining completions
            co_return;
        }

        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &(client_addr_.sin6_addr), ip_str, INET6_ADDRSTRLEN);

        cout << "[LANDING" << (use_tls_ ? " TLS" : "") << "] Accept with [" << ip_str << "]:"
                << (client_addr_.sin6_port) << endl;

        auto client_file = vector<shared_ptr<File>>{make_shared<File>(connection)};

        auto it = find(servers_.begin(), servers_.end(), nullopt);

        size_t loc;

        if (it == servers_.end()) {
            // How do you in-place construct a deeply nested vector element. Very ugly!?
            servers_.emplace_back(in_place, piecewise_construct,
                                  forward_as_tuple(std::move(client_file), use_tls_, &http_manager_),
                                  forward_as_tuple(nullopt));
            it = --servers_.end();
        } else {
            it->emplace(piecewise_construct,
                        forward_as_tuple(std::move(client_file), use_tls_, &http_manager_),
                        forward_as_tuple(nullopt));
        }

        auto& [server, _] = it->value();
        auto task = server.start().then([it](int return_code) -> void {
            cout << "Connection Closed" << endl;
            if (it->value().second) {
                it->value().second.value().detach();
            }
            *it = nullopt;

        });

        if (*it) {
            it->value().second = std::move(task);
        }
    }
}
