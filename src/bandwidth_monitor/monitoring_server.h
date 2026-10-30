#pragma once

#include <boost/beast/http/string_body.hpp>
#include <optional>
#include <vector>
#include <string>
#include <cstring>
#include <openssl/sha.h>

#include "defs.h"
#include "event.h"
#include "http_manager.h"

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
    BandwidthMonitoringServer(vector<shared_ptr<File>> client_file, bool enable_tls, const HTTPManager* http_manager);

    CallResponse start(uint64_t);

    optional<pair<bool, int>> on_yield(EventType event) override;

    std::string get_info() const override;

private:
    optional<pair<bool, int>> stage_http_read(ChildTaskCompletion& result);

    void stage_websocket_init();

    void stage_websocket_logs();

    void stage_websocket_live(ChildTaskCompletion& result);

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
