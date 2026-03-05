#pragma once

#include <boost/beast/http/string_body.hpp>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

#include <openssl/sha.h>

#include "common/defs.hpp"
#include "websocket_consumer_event.hpp"
#include "common/io_event.hpp"
#include "common/io_uring_manager.hpp"
#include "bandwidth_data_write_event.hpp"
#include "common/inet_socket_read_write_event_http.hpp"
#include "common/http_manager.hpp"

extern std::string base64_encode(const unsigned char *data, size_t input_length);

class BandwidthMonitoringServer : public IoEvent {
public:
    BandwidthMonitoringServer(std::shared_ptr<File> client_file, bool enable_tls, const HTTPManager& http_manager)
        : IoEvent(client_file), http_manager_(http_manager)
    {
        IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventHTTP>(this, client_file, enable_tls);
    }

    CallResponse start(uint64_t) {
        IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
            this,
            0,
            &InetSocketReadWriteEventHTTP::read_http
        );
        current_op_ = Read;
        return {"Starting up bandwidth monitor.", true, OP_HINT_NETWORK};
    }

    void on_new_data(int, EventType event) override {
        auto res = std::get<ChildTaskCompletion>(event);
        if (res.return_code <= 0) {
            delete this;
            return;
        }

        switch (current_op_) {
            case Read:
            {
                auto& req = IoUringManager::getInstance().get_data<InetSocketReadWriteEventHTTP>(this, 0, res.task_id).value()->get();

                if (req.method() == http::verb::get) {
                    if (req.find("Connection") != req.end() && req.find("Upgrade") != req.end()) {
                        IoUringManager::getInstance().finalize_current_task(true, -1);;
                        return; //for now
                        if (req.at("Connection") != "upgrade" && req.at("Upgrade") != "websocket") {
                            IoUringManager::getInstance().finalize_current_task(true, -1);;
                            return;
                        }
                        current_op_ = WebsocketTransition;

                        auto key = req.at("Sec-WebSocket-Key");
                        std::string combined = std::string(key) + GUID;
                        unsigned char hash[SHA_DIGEST_LENGTH];
                        SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);
                        std::string accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);

                        http::response<http::string_body> res{http::status::switching_protocols, req.version()};
                        res.set(http::field::connection, "Upgrade");
                        res.set(http::field::upgrade, "websocket");
                        res.set(http::field::sec_websocket_accept, accept_key);
                        res.prepare_payload();

                        IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                            this,
                            0,
                            &InetSocketReadWriteEventHTTP::write_http,
                            res
                        );
                        return;
                    }
                    IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                        this,
                        0,
                        &InetSocketReadWriteEventHTTP::write_http,
                        http_manager_.handle_request(req)
                    );
                    current_op_ = Write;
                } else {
                    IoUringManager::getInstance().finalize_current_task(true, -1);
                }
                break;
            }
            case Write:
                IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                    this,
                    0,
                    &InetSocketReadWriteEventHTTP::read_http
                );
                current_op_ = Read;
                break;
            case WebsocketTransition:
                // Have our HTTP adaptor class grant us control of the underlying socket class and then quit itself
                // TODO
                std::cout << "Websocket transition finished. Stalling." << std::endl;
                current_op_ = Websocket;
                break;
        }
    }

    std::string get_info() const override { return "BandwidthDataReadEvent on FD " + std::to_string(file_->get()); }

private:
    enum State {
        Read,
        Write,
        WebsocketTransition,
        Websocket
    };

    State current_op_;
    const HTTPManager& http_manager_;
    std::vector<unsigned char> buffer_;
    std::string accumulated_;
    const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
};
