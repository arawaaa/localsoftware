#pragma once

#include <cstdint>
#include <limits>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>

#include "inet_socket_read_write_event_bytes.cpp"
#include "io_event.cpp"
#include "io_uring_manager.cpp"
#include "inet_socket_tls_event.cpp"
#include "defs.cpp"

using namespace std;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class WebsocketEvent : public IoEvent {
public:
    WebsocketEvent(bool tls_enabled = false)
        : tls_enabled_(tls_enabled)
    {
        /* Child Events must be moved from the http class, since a websocket conn must be started from a http context */
    }

    virtual ~WebsocketEvent() {
        delete[] w_buf_;
    }
    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    CallResponse write_frame(uint64_t, char* buf, size_t len) {
        w_len_ = 2 + (len > 125 ? (len > numeric_limits<uint16_t>::max() ? 8 : 2): 0) + len;
        w_buf_ = new char[w_len_];
        w_buf_[0] = OpHeader::FIN | OpHeader::Binary;
        size_t idx = 2;
        if (len <= 125) {
            w_buf_[1] = (0x0 << 7) | len;
        } else if (len <= numeric_limits<uint16_t>::max()) {
            w_buf_[1] = (0x0 << 7) | 0x7E;
            // Big endian format
            w_buf_[2] = len >> 8 & 0xFF;
            w_buf_[3] = len & 0xFF;
            idx = 4;
        } else {
            w_buf_[1] = (0x0 << 7) | 0x7F;
            w_buf_[2] = 0;
            w_buf_[3] = 0;
            w_buf_[4] = 0;
            w_buf_[5] = 0;
            w_buf_[6] = len >> 24 & 0xFF;
            w_buf_[7] = len >> 16 & 0xFF;
            w_buf_[8] = len >> 8 & 0xFF;
            w_buf_[9] = len & 0xFF;
            idx = 10;
        }

        memcpy(w_buf_ + idx, buf, len);

        arm_write();
        return {"Websocket Write", true, OP_HINT_WRITE};
    }

    CallResponse read_frame(uint64_t, char** buf) {
        // TODO implement
        r_buf_ = buf;
        return {"Websocket Read", true, OP_HINT_READ};
    }

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    void on_new_data(int, EventType event) override {
        auto res = get<ChildTaskCompletion>(event);
        if (res.return_code <= 0) {
            IoUringManager::getInstance().finalize_current_task(true, res.return_code);
            return;
        }

        delete[] w_buf_;
        w_buf_ = nullptr;
        IoUringManager::getInstance().finalize_current_task(false, res.return_code);
    }

    string get_info() const override {
        return "HTTP parser";
    }

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

    void arm_write() {
        if (tls_enabled_) {
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketTLSEvent>(
                this,
                0,
                &InetSocketTLSEvent::write,
                w_buf_,
                w_len_
            );
            taskid_writer_ = taskid;
        } else {
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventBytes>(
                this,
                0,
                &InetSocketReadWriteEventBytes::write,
                w_buf_,
                w_len_
            );
            taskid_writer_ = taskid;

        }
    }

    enum Type {
        Header,
        Length1,
        Length2,
        Content
    };

    void arm_read(int len, int ) {
        if (tls_enabled_) {
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketTLSEvent>(
                this,
                0,
                &InetSocketTLSEvent::read,
                w_buf_,
                w_len_,
                false
            );
            taskid_writer_ = taskid;
        } else {
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventBytes>(
                this,
                0,
                &InetSocketReadWriteEventBytes::write,
                w_buf_,
                w_len_
            );
            taskid_writer_ = taskid;

        }
    }
};

