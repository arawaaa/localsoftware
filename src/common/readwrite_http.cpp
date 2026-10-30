#include "readwrite_http.h"

#include <memory>
#include <utility>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/optional/optional_io.hpp>

#include "readwrite_bytes.h"
#include "event.h"
#include "tls_socket.h"
#include "defs.h"

using namespace std;
namespace http = boost::beast::http;

ReadWriteHTTP::ReadWriteHTTP(vector<shared_ptr<File>> file, bool use_ssl)
    : Event(file), tls_enabled_(use_ssl)
{
    if (tls_enabled_)
        i<TLSSocket>(files_, true);
    else
        i<ReadWriteBytes>(files_);
}

CallResponse ReadWriteHTTP::read_http(uint64_t) {
    read_op_ = true;
    parser_ = make_unique<http::request_parser<http::string_body>>();

    arm_read();
    return {"Read HTTP", true, nullopt, OP_HINT_READ};
}

unique_ptr<http::request_parser<http::string_body>> ReadWriteHTTP::get_data(uint64_t) {
    return std::move(parser_);
}

CallResponse ReadWriteHTTP::write_http(uint64_t, http::response<http::string_body> res) {
    read_op_ = false;
    http::response_serializer<http::string_body> sr{res};
    boost::system::error_code ec;
    while (!sr.is_done()) {
        sr.next(ec, [&](auto const&, auto const& src) {
            // Copy from src (ConstBufferSequence) to write_buffer_
            size_t n = boost::asio::buffer_copy(write_buffer_.prepare(boost::asio::buffer_size(src)), src);
            write_buffer_.commit(n);
            sr.consume(n);
        });
        if (ec) break;
    }

    arm_write();
    return {"HTTP Write", true, nullopt, OP_HINT_WRITE};
}

CallResponse ReadWriteHTTP::write_http_with_file_body(uint64_t, http::response_header<> header, std::string file, int dirfd) {

}

optional<pair<bool, int>> ReadWriteHTTP::on_yield(EventType event) {
    auto res = get<ChildTaskCompletion>(event);
    if (res.return_code <= 0) {
        return pair{true, res.return_code};
    }

    if (read_op_) {
        return handle_read(res.return_code);
    } else {
        return handle_write(res.return_code);
    }
}

string ReadWriteHTTP::get_info() const {
    return "HTTP parser";
}

optional<pair<bool, int>> ReadWriteHTTP::handle_read(int res) {
    if (res > 0) {
        buffer_.commit(res);
    }

    bool done = try_parse();
    if (done) {
        return pair{false, 1};
    }

    arm_read();
    return nullopt;
}

optional<pair<bool, int>> ReadWriteHTTP::handle_write(int res) {
    if (res > 0) {
        write_buffer_.consume(res);
    }

    if (write_buffer_.size() > 0) {
        arm_write();
        return nullopt;
    }
    return pair{false, 1};
}

void ReadWriteHTTP::arm_read() {
    // Prepare space in the flat_buffer for the next read
    auto mutable_buffer = buffer_.prepare(4096);
    if (tls_enabled_) {
        uint64_t taskid = c(&TLSSocket::read, (char*)mutable_buffer.data(), 4096, false);
        taskid_reader_ = taskid;
    } else {
        uint64_t taskid = c(&ReadWriteBytes::read, (char*)mutable_buffer.data(), 4096, false);
        taskid_reader_ = taskid;
    }
}

void ReadWriteHTTP::arm_write() {
    if (write_buffer_.size() == 0) return;
    if (tls_enabled_) {
        uint64_t taskid = c(&TLSSocket::write, (char*)write_buffer_.data().data(), write_buffer_.size());
        taskid_writer_ = taskid;
    } else {
        uint64_t taskid = c(&ReadWriteBytes::write, (char*)write_buffer_.data().data(), write_buffer_.size());
        taskid_writer_ = taskid;

    }
}

bool ReadWriteHTTP::try_parse() {
    boost::system::error_code ec;
    size_t consumed = parser_->put(buffer_.data(), ec);
    buffer_.consume(consumed);

    while (!ec && !parser_->is_done() && buffer_.size()) {
        consumed = parser_->put(buffer_.data(), ec);
        buffer_.consume(consumed);
    }

    if (ec && ec != http::error::need_more) {
        // Treat parse errors as "done" so the caller can handle the error state in the parser
        return true;
    }

    return parser_->is_done();
}
