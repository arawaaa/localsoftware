#pragma once

#include <boost/beast/http/string_body.hpp>
#include <optional>
#include <variant>
#include <vector>
#include <string>
#include <cstring>
#include <cctype>

#include <openssl/sha.h>

#include "bandwidth_data_timer_event.cpp"
#include "common/defs.cpp"
#include "common/inet_socket_read_write_event_bytes.cpp"
#include "common/inet_socket_tls_event.cpp"
#include "common/io_event.cpp"
#include "common/io_uring_manager.cpp"
#include "common/inet_socket_read_write_event_http.cpp"
#include "common/inet_websocket_event.cpp"
#include "common/http_manager.cpp"

extern const std::string LOG_FILE;
extern double global_rx_speed;
extern double global_tx_speed;
extern std::mutex speed_mutex;
using namespace std;

const char encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const int mod_table[] = {0, 2, 1};

struct __attribute__((packed)) LogRecord {
    uint64_t timestamp;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint32_t hour;
};

class BandwidthMonitoringServer : public Event {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    BandwidthMonitoringServer(vector<shared_ptr<File>> client_file, bool enable_tls, const HTTPManager* http_manager)
        : Event(client_file), tls_enabled_(enable_tls), http_manager_(http_manager)
    {
        i_s<InetSocketReadWriteEventHTTP>(client_file, enable_tls);
    }

    CallResponse start(uint64_t) {
        c(&InetSocketReadWriteEventHTTP::read_http);
        timer_ = timer(ts);
        current_op_ = Read;
        return {"Starting up bandwidth monitor.", true, nullopt, OP_HINT_NETWORK};
    }

    optional<pair<bool, int>> on_yield(EventType event) override {
        return visit(overloaded {
            [this](ChildTaskCompletion& child) -> optional<pair<bool, int>> {
                if (child.return_code <= 0) {
                    return pair {true, child.return_code};
                }

                switch (current_op_) {
                    case Read:
                        cancel_timer(timer_);
                        return stage_http_read(child);
                        break;
                    case Write:
                        timer_ = timer(ts);
                        c(&InetSocketReadWriteEventHTTP::read_http);
                        current_op_ = Read;
                        return nullopt;
                    case WebsocketInit:
                        // Allow streaming for 20 minutes
                        timer_ = timer(__kernel_timespec {.tv_sec = 1200, .tv_nsec = 0});
                        stage_websocket_init();
                    case WebsocketLogs:
                        stage_websocket_logs();
                        break;
                    case WebsocketLive:
                        stage_websocket_live(child);
                        break;
                }
                return nullopt;
            },
            [](Timeout&) -> optional<pair<bool, int>> {
                return pair{true, -1};
            },
            [](auto&) -> optional<pair<bool, int>> {
                return nullopt;
            }
        }, event);
    }

    std::string get_info() const override { return "BandwidthDataReadEvent on FD " + std::to_string(files_[0]->get()); }

private:
    optional<pair<bool, int>> stage_http_read(ChildTaskCompletion& result) {
        auto req = direct_access(0, &InetSocketReadWriteEventHTTP::get_data, result.task_id).value()->get();

        if (req.method() == http::verb::get) {
            if (req.find("Connection") != req.end() && req.find("Upgrade") != req.end()) {
                if (req.at("Connection") != "Upgrade" || req.at("Upgrade") != "websocket") {
                    return pair {true, -1};
                }
                current_op_ = WebsocketInit;

                auto key = req.at("Sec-WebSocket-Key");
                string combined = string(key) + GUID;
                unsigned char hash[SHA_DIGEST_LENGTH];
                SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);
                string accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);

                http::response<http::string_body> res{http::status::switching_protocols, req.version()};
                res.set(http::field::connection, "Upgrade");
                res.set(http::field::upgrade, "websocket");
                res.set(http::field::sec_websocket_accept, accept_key);
                res.prepare_payload();

                c(&InetSocketReadWriteEventHTTP::write_http, res);
                return nullopt;
            }
            c(&InetSocketReadWriteEventHTTP::write_http,
                http_manager_->handle_request(req)
            );
            current_op_ = Write;
        } else {
            return pair {true, -1};
        }
        return nullopt;
    }

    void stage_websocket_init() {
        // Have our HTTP adaptor class grant us control of the underlying socket class and then quit itself

        i_s<BandwidthDataTimerEvent>();
        i_s<WebsocketEvent>(tls_enabled_);

        log_.open(LOG_FILE);
        current_op_ = WebsocketLogs;
    }

    void stage_websocket_logs() {
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
            c(&WebsocketEvent::write_frame, (char*)marker, sizeof(marker));
            current_op_ = WebsocketLive;
        } else {
            c(&WebsocketEvent::write_frame, chunk_data.data(), chunk_data.size());
        }
    }

    void stage_websocket_live(ChildTaskCompletion& result) {
        if (result.task_id == websocket_writerid_) write_occurred_  = true;
        else if (result.task_id == timerid_) timeout_occurred_ = true;

        if ((write_occurred_ && timeout_occurred_) || timerid_ == 0) {
            uint64_t rx, tx;
            {
                std::lock_guard<std::mutex> lock(speed_mutex);
                rx = static_cast<uint64_t>(global_rx_speed);
                tx = static_cast<uint64_t>(global_tx_speed);
            }

            time_t now = time(nullptr);
            struct tm *t = localtime(&now);

            rec.timestamp = now;
            rec.rx_bytes = rx;
            rec.tx_bytes = tx;
            rec.hour = t->tm_hour;

            auto tid = c(&BandwidthDataTimerEvent::prepare_timer);
            auto wid = c(&WebsocketEvent::write_frame, (char*)&rec, sizeof(rec));

            write_occurred_ = false;
            timeout_occurred_ = false;
            timerid_ = tid;
            websocket_writerid_ = wid;
        }
    }

    static string base64_encode(const unsigned char *data, size_t input_length) {
        size_t output_length = 4 * ((input_length + 2) / 3);
        string encoded_data(output_length, '\0');

        for (size_t i = 0, j = 0; i < input_length;) {
            uint32_t octet_a = i < input_length ? data[i++] : 0;
            uint32_t octet_b = i < input_length ? data[i++] : 0;
            uint32_t octet_c = i < input_length ? data[i++] : 0;

            uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

            encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
            encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
            encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
            encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
        }

        for (int i = 0; i < mod_table[input_length % 3]; i++)
            encoded_data[output_length - 1 - i] = '=';

        return encoded_data;
    }

    const __kernel_timespec ts = {.tv_sec = 60, .tv_nsec=0};
    uint64_t timer_;

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
    const HTTPManager* http_manager_;
    std::vector<unsigned char> buffer_;
    std::string accumulated_;
    const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint64_t marker[3] = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};
    LogRecord rec;
};
