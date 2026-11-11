#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>
#include <optional>

#include "landing_accept.h"
#include "landing_server.h"
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
        int connection = co_await ca(&io_uring_prep_accept, files_[0]->get(), reinterpret_cast<struct sockaddr*>(&client_addr_),
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

        int idx = i<AioLandingHTTP>(std::move(client_file), use_tls_, &http_manager_);
        uint64_t taskid = c(idx, &AioLandingHTTP::start);
        indices[taskid] = idx;
    }
}

optional<pair<bool, int>> LandingAccept::on_yield(EventType event) {
    if (holds_alternative<ChildTaskCompletion>(event)) {
        std::cout << "Connection close" << std::endl;
        auto res = get<ChildTaskCompletion>(event);
        d<AioLandingHTTP>(indices[res.task_id]);
        indices.erase(res.task_id);
    }
    return nullopt;
}

string LandingAccept::get_info() const {
    return "LandingAccept FD " + to_string(files_[0]->get());
}
