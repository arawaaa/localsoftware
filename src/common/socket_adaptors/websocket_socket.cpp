#include "websocket_socket.h"

#include <cstdint>
#include <limits>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>

#include "readwrite_bytes.h"
#include "event.h"
#include "tls_socket.h"
#include "defs.h"

using namespace std;

WebsocketSocket::WebsocketSocket(bool tls_enabled)
    : tls_enabled_(tls_enabled)
{
    /* Child Events must be moved from the http class, since a websocket conn must be started from a http context */
}

WebsocketSocket::~WebsocketSocket() {
    delete[] w_buf_;
}

CallResponse WebsocketSocket::write_frame2(uint64_t id, string& str) {
    return write_frame(id, str.data(), str.size());
}

CallResponse WebsocketSocket::write_frame(uint64_t, char* buf, size_t len) {
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
    return {"Websocket Write", true, nullopt, OP_HINT_WRITE};
}

CallResponse WebsocketSocket::read_frame(uint64_t, char** buf) {
    // TODO implement
    r_buf_ = buf;
    return {"Websocket Read", true, nullopt, OP_HINT_READ};
}

optional<pair<bool, int>> WebsocketSocket::on_yield(EventType event) {
    auto res = get<ChildTaskCompletion>(event);
    if (res.return_code <= 0) {
        return pair{true, res.return_code};
    }

    delete[] w_buf_;
    w_buf_ = nullptr;
    return pair{false, res.return_code};
}

string WebsocketSocket::get_info() const {
    return "HTTP parser";
}

void WebsocketSocket::arm_write() {
    if (tls_enabled_) {
        auto taskid = c(&TLSSocket::write, w_buf_, w_len_);
        taskid_writer_ = taskid;
    } else {
        auto taskid = c(&ReadWriteBytes::write, w_buf_, w_len_);
        taskid_writer_ = taskid;
    }
}

void WebsocketSocket::arm_read(int, int) {
    if (tls_enabled_) {
        auto taskid = c(&TLSSocket::read, w_buf_, w_len_, false);
        taskid_writer_ = taskid;
    } else {
        auto taskid = c(&ReadWriteBytes::write, w_buf_, w_len_);
        taskid_writer_ = taskid;

    }
}
