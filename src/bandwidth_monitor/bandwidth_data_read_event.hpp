#pragma once

#include "../common/io_event.hpp"
#include "../common/io_uring_manager.hpp"
#include "bandwidth_data_write_event.hpp"
#include "websocket_consumer_event.hpp"
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <openssl/sha.h>
#include <algorithm>
#include <cctype>
#include <iostream>

extern std::string base64_encode(const unsigned char *data, size_t input_length);

class BandwidthDataReadEvent : public IoEvent {
public:
    BandwidthDataReadEvent(std::unique_ptr<File> client_file) 
        : IoEvent(std::move(client_file)) {
        buffer_.resize(2048);
    }

    void prepare_read() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_recv, fd_, buffer_.data(), buffer_.size(), 0);
    }

    void on_new_data(int op, EventType event) override {
        int res = std::get<IoUringResult>(event).res;
        std::cout << res << std::endl;
        if (res <= 0) {
            delete this;
            return;
        }
        
        accumulated_.append(reinterpret_cast<char*>(buffer_.data()), res);
        
        if (accumulated_.find("\r\n\r\n") == std::string::npos) {
            if (accumulated_.length() > 8192) {
                delete this;
                return;
            }
            prepare_read();
            return;
        }

        std::string lower = accumulated_;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });

        if (lower.find("upgrade: websocket") != std::string::npos) {
            handle_websocket_upgrade(accumulated_);
            // ownership of file_ was moved inside handle_websocket_upgrade
            delete this;
            return;
        } else if (lower.find("get / ") != std::string::npos || lower.find("get /http") != std::string::npos) {
            handle_http_get();
            // ownership was moved
            delete this;
            return;
        } else {
            delete this;
            return;
        }
    }

    std::string get_info() const override { return "BandwidthDataReadEvent on FD " + std::to_string(fd_); }

private:
    std::vector<unsigned char> buffer_;
    std::string accumulated_;
    const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    void handle_websocket_upgrade(const std::string& req) {
        std::cout << "[WS] Upgrade requested on FD " << fd_ << std::endl;
        size_t key_pos = req.find("Sec-WebSocket-Key:");
        if (key_pos == std::string::npos) {
            key_pos = req.find("sec-websocket-key:");
        }

        if (key_pos == std::string::npos) {
            return;
        }

        key_pos += 18;
        while (key_pos < req.length() && (req[key_pos] == ' ' || req[key_pos] == '\t')) key_pos++;
        size_t eol = req.find("\r\n", key_pos);
        if (eol == std::string::npos) return;
        
        std::string key = req.substr(key_pos, eol - key_pos);
        size_t last = key.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) key = key.substr(0, last + 1);

        std::string combined = key + GUID;
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);
        std::string accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);

        std::ostringstream oss;
        oss << "HTTP/1.1 101 Switching Protocols\r\n"
            << "Upgrade: websocket\r\n"
            << "Connection: Upgrade\r\n"
            << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
            << "\r\n";

        // Capture raw FD for the non-owning consumer
        int raw_fd = fd_;

        // Move ownership of the File object to the WriteEvent
        auto* write_ev = new BandwidthDataWriteEvent(std::move(file_), oss.str(), true);
        write_ev->prepare_write();

        // Start the non-owning consumer to handle Pings/Close from client
        auto* consumer = new WebSocketConsumerEvent(raw_fd);
        consumer->prepare_consumer();
    }

    void handle_http_get() {
        std::cout << "[HTTP] GET / from FD " << fd_ << std::endl;
        std::string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
        // Move ownership of the File object to the WriteEvent
        auto* write_ev = new BandwidthDataWriteEvent(std::move(file_), header, false);
        write_ev->prepare_write();
    }
};
