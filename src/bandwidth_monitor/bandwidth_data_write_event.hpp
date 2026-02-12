#pragma once

#include "../common/io_event.hpp"
#include "../common/io_uring_manager.hpp"
#include "bandwidth_data_timer_event.hpp"
#include <string>
#include <cstring>
#include <fstream>
#include <vector>
#include <mutex>
#include <iostream>
#include <iomanip>
#include <unistd.h>

extern const std::string LOG_FILE;
extern double global_rx_speed;
extern double global_tx_speed;
extern std::mutex speed_mutex;

struct __attribute__((packed)) LogRecord {
    uint64_t timestamp;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint32_t hour; 
};

class BandwidthDataWriteEvent : public IoEvent {
public:
    enum State {
        STATE_INITIAL_WRITE,
        STATE_SENDING_LOGS,
        STATE_SENDING_MARKER,
        STATE_WAITING_TIMER,
        STATE_SENDING_LIVE,
        STATE_SENDING_FILE,
        STATE_DONE
    };

    BandwidthDataWriteEvent(std::unique_ptr<File> client_file, std::string initial_response, struct io_uring* ring, bool is_websocket) 
        : IoEvent(std::move(client_file)), 
          response_(std::move(initial_response)), 
          ring_(ring), 
          sent_bytes_(0),
          is_websocket_(is_websocket),
          state_(STATE_INITIAL_WRITE),
          timer_event_(nullptr) {
    }

    ~BandwidthDataWriteEvent() {
        if (timer_event_) delete timer_event_;
        if (file_stream_.is_open()) file_stream_.close();
    }

    void prepare_write() {
        const char* data_ptr = response_.c_str() + sent_bytes_;
        size_t remaining = response_.length() - sent_bytes_;
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_send, fd_, data_ptr, remaining, MSG_NOSIGNAL);
    }

    void post(int id, int res) override {
        if (res <= 0) {
            delete this; 
            return; 
        }

        sent_bytes_ += res;

        if (sent_bytes_ < response_.length()) {
            prepare_write();
            return;
        }

        sent_bytes_ = 0;
        response_.clear();

        switch (state_) {
            case STATE_INITIAL_WRITE:
                if (is_websocket_) {
                    state_ = STATE_SENDING_LOGS;
                    file_stream_.open(LOG_FILE);
                    prepare_next_log_chunk();
                } else {
                    state_ = STATE_SENDING_FILE;
                    file_stream_.open("/srv/monitoringindex.html");
                    prepare_next_file_chunk();
                }
                break;

            case STATE_SENDING_LOGS:
                prepare_next_log_chunk();
                break;

            case STATE_SENDING_MARKER:
                state_ = STATE_WAITING_TIMER;
                schedule_timer();
                break;

            case STATE_SENDING_LIVE:
                state_ = STATE_WAITING_TIMER;
                schedule_timer();
                break;

            case STATE_SENDING_FILE:
                prepare_next_file_chunk();
                break;

            case STATE_DONE:
                delete this; 
                break;

            default:
                break;
        }
    }

    void trigger_live_update() {
        if (state_ == STATE_WAITING_TIMER) {
            state_ = STATE_SENDING_LIVE;
            prepare_live_data();
            prepare_write();
        }
    }

    std::string get_info() const override { return "BandwidthDataWriteEvent on FD " + std::to_string(fd_); }

private:
    std::string response_;
    struct io_uring* ring_;
    size_t sent_bytes_;
    bool is_websocket_;
    State state_;
    std::ifstream file_stream_;
    BandwidthDataTimerEvent* timer_event_;

    void schedule_timer() {
        if (!timer_event_) {
            timer_event_ = new BandwidthDataTimerEvent(this, ring_);
        }
        // Timer event handles its own prep
        timer_event_->prepare_timer();
    }

    std::string wrap_ws(const void* data, size_t len) {
        std::string frame;
        frame.push_back(static_cast<char>(0x82)); // FIN + Binary
        if (len <= 125) {
            frame.push_back(static_cast<char>(len));
        } else {
            frame.push_back(126);
            frame.push_back(static_cast<char>((len >> 8) & 0xFF));
            frame.push_back(static_cast<char>(len & 0xFF));
        }
        frame.append(reinterpret_cast<const char*>(data), len);
        return frame;
    }

    void prepare_next_file_chunk() {
        char chunk_buf[4096];
        if (file_stream_.is_open() && file_stream_.read(chunk_buf, sizeof(chunk_buf))) {
            response_.assign(chunk_buf, file_stream_.gcount());
            prepare_write();
        } else if (file_stream_.gcount() > 0) {
            response_.assign(chunk_buf, file_stream_.gcount());
            prepare_write();
        } else {
            state_ = STATE_DONE;
            // Finish HTTP request
            delete this;
        }
    }

    void prepare_next_log_chunk() {
        if (!file_stream_.is_open()) {
            state_ = STATE_SENDING_MARKER;
            uint64_t marker[3] = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};
            response_ = wrap_ws(marker, sizeof(marker));
            prepare_write();
            return;
        }

        std::string line;
        int count = 0;
        std::string chunk_data;

        while (count < 15 && std::getline(file_stream_, line)) {
            long long ts; int d, m, y, h; unsigned long long tx, rx;
            if (sscanf(line.c_str(), "[%lld] %d/%d/%d %d %llu %llu", &ts, &d, &m, &y, &h, &tx, &rx) == 7) {
                LogRecord rec = {(uint64_t)ts, (uint64_t)tx, (uint64_t)rx, (uint32_t)h};
                chunk_data.append(reinterpret_cast<const char*>(&rec), sizeof(rec));
                count++;
            }
        }

        if (chunk_data.empty()) {
            file_stream_.close();
            state_ = STATE_SENDING_MARKER;
            uint64_t marker[3] = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};
            response_ = wrap_ws(marker, sizeof(marker));
            prepare_write();
        } else {
            response_ = wrap_ws(chunk_data.data(), chunk_data.size());
            prepare_write();
        }
    }

    void prepare_live_data() {
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
        
        response_ = wrap_ws(&rec, sizeof(rec));
    }
};

inline BandwidthDataTimerEvent::BandwidthDataTimerEvent(BandwidthDataWriteEvent* writer, struct io_uring* ring)
    : IoEvent(-1), writer_(writer), ring_(ring) {}

inline void BandwidthDataTimerEvent::post(int id, int res) {
    writer_->trigger_live_update();
}