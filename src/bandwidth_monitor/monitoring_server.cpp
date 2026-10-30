#include "monitoring_server.h"

#include <boost/beast/http/string_body.hpp>
#include <optional>
#include <variant>
#include <vector>
#include <string>

#include <openssl/sha.h>

#include "bandwidth_data_timer.h"
#include "defs.h"
#include "event.h"
#include "readwrite_http.h"
#include "websocket_socket.h"
#include "http_manager.h"

BandwidthMonitoringServer::BandwidthMonitoringServer(vector<shared_ptr<File>> client_file, bool enable_tls, const HTTPManager* http_manager)
    : Event(client_file), tls_enabled_(enable_tls), http_manager_(http_manager)
{
    i_s<ReadWriteHTTP>(client_file, enable_tls);
}

CallResponse BandwidthMonitoringServer::start(uint64_t) {
    c(&ReadWriteHTTP::read_http);
    timer_ = timer(ts);
    current_op_ = Read;
    return {"Starting up bandwidth monitor.", true, nullopt, OP_HINT_NETWORK};
}

optional<pair<bool, int>> BandwidthMonitoringServer::on_yield(EventType event) {
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
                    c(&ReadWriteHTTP::read_http);
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

std::string BandwidthMonitoringServer::get_info() const {
    return "BandwidthDataReadEvent on FD " + std::to_string(files_[0]->get());
}

optional<pair<bool, int>> BandwidthMonitoringServer::stage_http_read(ChildTaskCompletion& result) {
    auto req = direct_access(0, &ReadWriteHTTP::get_data, result.task_id).value()->get();

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

            c(&ReadWriteHTTP::write_http, res);
            return nullopt;
        }
        c(&ReadWriteHTTP::write_http,
            http_manager_->handle_request(req)
        );
        current_op_ = Write;
    } else {
        return pair {true, -1};
    }
    return nullopt;
}

void BandwidthMonitoringServer::stage_websocket_init() {
    // Have our HTTP adaptor class grant us control of the underlying socket class and then quit itself

    i_s<BandwidthDataTimer>();
    i_s<WebsocketSocket>(tls_enabled_);

    log_.open(LOG_FILE);
    current_op_ = WebsocketLogs;
}

void BandwidthMonitoringServer::stage_websocket_logs() {
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
        c(&WebsocketSocket::write_frame, (char*)marker, sizeof(marker));
        current_op_ = WebsocketLive;
    } else {
        c(&WebsocketSocket::write_frame2, chunk_data);
    }
}

void BandwidthMonitoringServer::stage_websocket_live(ChildTaskCompletion& result) {
    if (result.task_id == websocket_writerid_) write_occurred_  = true;
    else if (result.task_id == timerid_) timeout_occurred_ = true;

    if ((write_occurred_ && timeout_occurred_) || timerid_ == 0) {
        uint64_t rx, tx;
        {
            std::lock_guard<std::mutex> lock(speed_mutex);
            rx = static_cast<uint64_t>(global_rx_speed);
            tx = static_cast<uint64_t>(global_tx_speed);
        }

        chrono::zoned_time now_time {chrono::current_zone(), chrono::system_clock::now()};
        chrono::hh_mm_ss now_hms(now_time.get_sys_time() - chrono::floor<chrono::days>(now_time.get_sys_time()));

        rec.timestamp = now_time.get_sys_time().time_since_epoch().count();
        rec.rx_bytes = rx;
        rec.tx_bytes = tx;
        rec.hour = now_hms.hours().count();

        auto tid = c(&BandwidthDataTimer::prepare_timer);
        auto wid = c(&WebsocketSocket::write_frame, (char*)&rec, sizeof(rec));

        write_occurred_ = false;
        timeout_occurred_ = false;
        timerid_ = tid;
        websocket_writerid_ = wid;
    }
}
