#pragma once

#include <boost/beast/http/string_body.hpp>
#include <variant>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

#include <openssl/sha.h>

#include "bandwidth_data_timer_event.hpp"
#include "common/defs.hpp"
#include "common/inet_socket_read_write_event_bytes.hpp"
#include "common/inet_socket_tls_event.hpp"
#include "common/io_event.hpp"
#include "common/io_uring_manager.hpp"
#include "common/inet_socket_read_write_event_http.hpp"
#include "common/inet_websocket_event.hpp"
#include "common/http_manager.hpp"


extern const std::string LOG_FILE;
extern double global_rx_speed;
extern double global_tx_speed;
extern std::mutex speed_mutex;
using namespace std;

extern string base64_encode(const unsigned char *data, size_t input_length);

struct __attribute__((packed)) LogRecord {
    uint64_t timestamp;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint32_t hour;
};

class BandwidthMonitoringServer : public IoEvent {
public:
    BandwidthMonitoringServer(vector<shared_ptr<File>> client_file, bool enable_tls, const HTTPManager& http_manager)
        : IoEvent(client_file), tls_enabled_(enable_tls), http_manager_(http_manager)
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
        auto result = std::get<ChildTaskCompletion>(event);
        if (result.return_code <= 0) {
            IoUringManager::getInstance().finalize_current_task(true, result.return_code);
            return;
        }

        switch (current_op_) {
            case Read:
            {
                auto req = IoUringManager::getInstance().get_data<InetSocketReadWriteEventHTTP>(this, 0, result.task_id).value()->get();

                if (req.method() == http::verb::get) {
                    if (req.find("Connection") != req.end() && req.find("Upgrade") != req.end()) {
                        if (req.at("Connection") != "Upgrade" || req.at("Upgrade") != "websocket") {
                            IoUringManager::getInstance().finalize_current_task(true, -1);
                            return;
                        }
                        current_op_ = WebsocketInit;

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

                        IoUringManager::getInstance().consume_event(result.task_id);
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
                    IoUringManager::getInstance().consume_event(result.task_id);
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
                IoUringManager::getInstance().consume_event(result.task_id);
                current_op_ = Read;
                break;
            case WebsocketInit:
                // Have our HTTP adaptor class grant us control of the underlying socket class and then quit itself

                IoUringManager::getInstance().initialize_dependent_event<BandwidthDataTimerEvent>(this); IoUringManager::getInstance().initialize_dependent_event<WebsocketEvent>(this, tls_enabled_);
                if (tls_enabled_) {
                    IoUringManager::getInstance().move_subevents<InetSocketReadWriteEventHTTP, WebsocketEvent, InetSocketTLSEvent>(this, 0, 0);
                } else {
                    IoUringManager::getInstance().move_subevents<InetSocketReadWriteEventHTTP, WebsocketEvent, InetSocketReadWriteEventBytes>(this, 0, 0);
                }
                log_.open(LOG_FILE);
                current_op_ = WebsocketLogs;
            case WebsocketLogs:
            {
                std::string line;
                int count = 0;
                std::string chunk_data;

                while (count < 15 && std::getline(log_, line)) {
                    long long ts; int d, m, y, h; unsigned long long tx, rx;
                    if (sscanf(line.c_str(), "[%lld] %d/%d/%d %d %llu %llu", &ts, &d, &m, &y, &h, &tx, &rx) == 7) {
                        LogRecord rec = {(uint64_t)ts, (uint64_t)tx, (uint64_t)rx, (uint32_t)h};
                        chunk_data.append(reinterpret_cast<const char*>(&rec), sizeof(rec));
                        count++;
                    }
                }

                if (chunk_data.empty()) {
                    uint64_t marker[3] = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};
                    IoUringManager::getInstance().call_dependent_function<WebsocketEvent>(
                        this,
                        0,
                        &WebsocketEvent::write_frame,
                        (char*)marker,
                        sizeof(marker)
                    );
                    current_op_ = WebsocketLive;
                } else {
                    IoUringManager::getInstance().call_dependent_function<WebsocketEvent>(
                        this,
                        0,
                        &WebsocketEvent::write_frame,
                        chunk_data.data(),
                        chunk_data.size()
                    );
                }
                IoUringManager::getInstance().consume_event(result.task_id);
                break;
            }
            case WebsocketLive:
                if (result.task_id == websocket_writerid_) write_occurred_  = true;
                else if (result.task_id == timerid_) timeout_occurred_ = true;

                if (write_occurred_ && timeout_occurred_ || timerid_ == 0) {
                    uint64_t rx, tx;
                    {
                        std::lock_guard<std::mutex> lock(speed_mutex);
                        rx = static_cast<uint64_t>(global_rx_speed);
                        tx = static_cast<uint64_t>(global_tx_speed);
                    }

                    time_t now = time(nullptr);
                    struct tm *t = localtime(&now);

                    LogRecord rec = {
                        (uint64_t)now,
                        tx,
                        rx,
                        (uint32_t)t->tm_hour
                    };

                    auto [tid, _] = IoUringManager::getInstance().call_dependent_function<BandwidthDataTimerEvent>(
                        this,
                        0,
                        &BandwidthDataTimerEvent::prepare_timer
                    );


                    auto [wid, _] = IoUringManager::getInstance().call_dependent_function<WebsocketEvent>(
                        this,
                        0,
                        &WebsocketEvent::write_frame,
                        (char*)&rec,
                        sizeof(rec)
                    );

                    write_occurred_ = false;
                    timeout_occurred_ = false;
                    timerid_ = tid;
                    websocket_writerid_ = wid;
                }


                IoUringManager::getInstance().consume_event(result.task_id);
        }
    }

    std::string get_info() const override { return "BandwidthDataReadEvent on FD " + std::to_string(files_[0]->get()); }

private:
    uint64_t timerid_ = 0, websocket_writerid_ = 0;
    bool tls_enabled_, write_occurred_ = false, timeout_occurred_ = false;
    ifstream log_;
    enum State {
        Read,
        Write,
        WebsocketInit,
        WebsocketLogs,
        WebsocketLive
    };

    State current_op_;
    const HTTPManager& http_manager_;
    std::vector<unsigned char> buffer_;
    std::string accumulated_;
    const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
};
