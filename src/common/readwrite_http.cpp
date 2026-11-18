#include "readwrite_http.h"

#include <memory>
#include <utility>
#include <iostream>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/optional/optional_io.hpp>
#include <variant>

#include "readwrite_bytes.h"
#include "event.h"
#include "tls_socket.h"
#include "defs.h"

using namespace std;
namespace http = boost::beast::http;

ReadWriteHTTP::ReadWriteHTTP(vector<shared_ptr<File>> file, bool use_ssl)
    : Event(file),
    socket_(use_ssl ? variant<ReadWriteBytes, TLSSocket>(in_place_type<TLSSocket>, file, true) :
        variant<ReadWriteBytes, TLSSocket>(in_place_type<ReadWriteBytes>, file)
    ),
    tls_enabled_(use_ssl)
{
}

pinned<variant<int, unique_ptr<http::request_parser<http::string_body>>>> ReadWriteHTTP::read_http() {
    parser_ = make_unique<http::request_parser<http::string_body>>();

    boost::beast::flat_buffer buffer;

    do {
        auto mutable_buffer = buffer.prepare(4096);
        int res;
        if (holds_alternative<TLSSocket>(socket_)) {
            res = co_await get<TLSSocket>(socket_).read((char*)mutable_buffer.data(), 4096, false);
        } else {
            res = co_await get<ReadWriteBytes>(socket_).read(mutable_buffer.data(), 4096, false);
        }

        if (res <= 0)
            co_return res;

        if (res > 0) {
            buffer.commit(res);
        }
    } while (!try_parse(buffer));

    co_return std::move(parser_);
}

pinned<int> ReadWriteHTTP::write_http(http::response<http::string_body> res) {
    http::response_serializer<http::string_body> sr{res};
    boost::system::error_code ec;
    boost::beast::flat_buffer write_buffer;
    while (!sr.is_done()) {
        sr.next(ec, [&](auto const&, auto const& src) {
            // Copy from src (ConstBufferSequence) to write_buffer_
            size_t n = boost::asio::buffer_copy(write_buffer.prepare(boost::asio::buffer_size(src)), src);
            write_buffer.commit(n);
            sr.consume(n);
        });
        if (ec) break;
    }

    if (write_buffer.size() == 0) co_return 0;

    if (holds_alternative<TLSSocket>(socket_)) {
        co_return co_await get<TLSSocket>(socket_).write((char*)write_buffer.data().data(), write_buffer.size());
    } else {
        co_return co_await get<ReadWriteBytes>(socket_).write(write_buffer.data().data(), write_buffer.size());
    }
}

CallResponse ReadWriteHTTP::write_http_with_file_body(uint64_t, http::response_header<> header, std::string file, int dirfd) {

}

bool ReadWriteHTTP::try_parse(boost::beast::flat_buffer& buffer) {
    boost::system::error_code ec;
    size_t consumed = parser_->put(buffer.data(), ec);
    buffer.consume(consumed);

    while (!ec && !parser_->is_done() && buffer.size()) {
        consumed = parser_->put(buffer.data(), ec);
        buffer.consume(consumed);
    }

    if (ec && ec != http::error::need_more) {
        // TODO SECURITY We MUST throw here. Implement handling for exceptions in coroutines
        return true;
    }

    return parser_->is_done();
}
