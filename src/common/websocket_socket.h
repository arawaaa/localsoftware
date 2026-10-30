#pragma once

#include <cstdint>
#include <limits>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>

#include "event.h"
#include "defs.h"

using namespace std;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class WebsocketSocket : public Event {
public:
    WebsocketSocket(bool tls_enabled = false);

    virtual ~WebsocketSocket();

    CallResponse write_frame2(uint64_t id, string& str);
    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    CallResponse write_frame(uint64_t, char* buf, size_t len);

    CallResponse read_frame(uint64_t, char** buf);

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    optional<pair<bool, int>> on_yield(EventType event) override;

    string get_info() const override;

protected:
    char headers_[128];
    char** r_buf_;
    char* w_buf_ = nullptr;
    size_t w_len_;
    bool read_op_;
    uint64_t taskid_writer_, taskid_reader_;

    enum OpHeader {
        // OR this with the opcode
        FIN = 0x8 << 4,
        RSV1 = 0x4 << 4,
        RSV2 = 0x2 << 4,
        RSV3 = 0x1 << 4,

        // Opcodes
        Continuation = 0,
        // Non-control frames
        Text = 1,
        Binary = 2,
        // 3-7 reserved for further non-control frames
        NoncontrolExtra1 = 3,
        NoncontrolExtra2,
        NoncontrolExtra3,
        NoncontrolExtra4,
        NoncontrolExtra5,
        // Control Frames
        Close = 8,
        Ping = 9,
        Pong = 10,
        // 11-15 reserved for further control frames
        ControlExtra1 = 11,
        ControlExtra2,
        ControlExtra3,
        ControlExtra4,
        ControlExtra5
    };

    bool tls_enabled_;

    void arm_write();

    enum Type {
        Header,
        Length1,
        Length2,
        Content
    };

    void arm_read(int, int);
};

