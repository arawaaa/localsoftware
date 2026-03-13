#pragma once

#include <arpa/inet.h>
#include <boost/beast/http/field.hpp>
#include <netdb.h>

#include "common/io_event.hpp"
#include "common/io_uring_manager.hpp"
#include "common/inet_socket_read_write_event_http.hpp"
#include "common/inet_socket_read_write_event_httpc.hpp"
#include "common/http_manager.hpp"
#include "reverse_proxy_manager.hpp"

class ReverseProxyServer : public IoEvent {
public:
    ReverseProxyServer(vector<shared_ptr<File>> file, const HTTPManager& http_manager, ReverseProxyManager& proxy_manager)
        : IoEvent(file), http_manager_(http_manager), proxy_manager_(proxy_manager)
    {
        // TLS always for reverse proxies
        IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventHTTP>(this, vector<shared_ptr<File>>{file[0]}, true);
    }

    CallResponse start(uint64_t) {
        IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
            this,
            0,
            &InetSocketReadWriteEventHTTP::read_http
        );
        next_op_ = WritePeerBranch;
        return {"Server start up HTTP", true, OP_HINT_NETWORK};
    }

    void on_new_data(int, EventType event) override {
        auto res = get<ChildTaskCompletion>(event);
        if (res.return_code <= 0) {
            IoUringManager::getInstance().finalize_current_task(true, -1);
            return;
        }

        switch (next_op_) {
            case WritePeerBranch:
            {
                auto req = IoUringManager::getInstance().get_data<InetSocketReadWriteEventHTTP>(this, 0, res.task_id).value()->get();

                auto resp = http_manager_.handle_request_with_pred(req, [this](const http::request<http::string_body>& req, bool& skip) -> optional<http::response<http::string_body>> {
                    if (auto it = req.find(http::field::cookie); it != req.end()) {
                        auto token = ReverseProxyManager::get_token(it->value());
                        if (token && proxy_manager_.check_auth(*token) && (connected_intern_peer_ || init_peer())) {
                            skip = false;
                            return nullopt; // Handled individually
                        } else if (!http_manager_.endpoint_serviceable(req.target())) {
                            // TODO redirect to landing
                            return http_manager_.error_response(req, http::status::unauthorized, "No authentication.");
                        }
                    }
                    skip = true;
                    return nullopt;
                });

                if (resp) {
                    IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                        this,
                        0,
                        &InetSocketReadWriteEventHTTP::write_http,
                        *resp
                    );
                    next_op_ = ReadExternalPeer;
                } else {
                    if (check_websocket(req))
                        check_websocket_ = true;
                    IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTPC>(
                        this,
                        0,
                        &InetSocketReadWriteEventHTTPC::write_http,
                        req
                    );
                    next_op_ = ReadInternalPeer;
                }
                break;
            }
            case ReadInternalPeer:
            {
                IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTPC>(
                    this,
                    0,
                    &InetSocketReadWriteEventHTTPC::read_http
                );
                next_op_ = WriteExternalPeer;
                break;
            }
            case WriteExternalPeer:
            {
                auto resp = IoUringManager::getInstance().get_data<InetSocketReadWriteEventHTTPC>(this, 0, res.task_id).value()->get();

                IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                    this,
                    0,
                    &InetSocketReadWriteEventHTTP::write_http,
                    resp
                );
                next_op_ = ReadExternalPeer;
                if (check_websocket_ && resp.result() == http::status::switching_protocols)
                    next_op_ = Websocket;
                break;
            }
            case ReadExternalPeer:
            {
                IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                    this,
                    0,
                    &InetSocketReadWriteEventHTTP::read_http
                );
                next_op_ = WritePeerBranch;
                break;
            }
            case Websocket:
            {

            }
        }
        IoUringManager::getInstance().consume_event(res.task_id);
    }

    string get_info() const override { return "ReverseProxyServer FD " + to_string(files_[0]->get()); }

private:
    bool check_websocket_ = false;
    uint64_t e_to_i_[2], i_to_e_[2];
    bool connected_intern_peer_ = false;
    enum State {
        ReadExternalPeer,
        ReadInternalPeer,
        WriteExternalPeer,
        WritePeerBranch,
        Websocket
    };

    State next_op_;
    const HTTPManager& http_manager_;
    ReverseProxyManager& proxy_manager_;

    bool init_peer() {
        struct addrinfo hints, *res, *result;
        int errcode;
        void *ptr;

        memset (&hints, 0, sizeof (hints));
        hints.ai_family = PF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags |= AI_CANONNAME;

        errcode = getaddrinfo(proxy_manager_.peer_addr.data(), "18789", &hints, &result);
        if (errcode != 0) {
            perror ("getaddrinfo");
            return false;
        }

        res = result;

        if (res) {
            int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (s == -1) {
                freeaddrinfo(result);
                perror("[Reverse Proxy socket]");
                return false;
            }

            int ret = connect(s, res->ai_addr, res->ai_addrlen);

            if (ret != 0) {
                perror("[Reverse Proxy connect]");
                freeaddrinfo(result);
                close(s);
                return false;
            }

            freeaddrinfo(result);
            IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventHTTPC>(this, vector<shared_ptr<File>>{make_shared<File>(s)}, false);
            connected_intern_peer_ = true;
            return true;
        }
        return false;
    }

    bool check_websocket(http::request<http::string_body>& req) {
        if (req.find(http::field::connection) != req.end() &&
            req.find(http::field::upgrade) != req.end()
        ) {
            if (req.at(http::field::connection) == "Upgrade" &&
                req.at(http::field::upgrade) == "websocket"
            ) {
                return true;
            }
        }
        return false;
    }
};
