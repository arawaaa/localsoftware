#include "readwrite_httpc.h"

#include <memory>
#include <utility>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>

#include "readwrite_bytes.h"
#include "event.h"
#include "inet_socket_tls_event.cpp"
#include "defs.h"

#include <boost/optional/optional_io.hpp>
using namespace std;
namespace http = boost::beast::http;


ReadWriteHTTPC::ReadWriteHTTPC(vector<shared_ptr<File>> file, bool use_ssl = false)
    : Event(file), tls_enabled_(use_ssl)
{
    if (tls_enabled_)
        AsyncHandler::self().initialize_dependent_event<InetSocketTLSEvent>(this, file, false);
    else
        AsyncHandler::self().initialize_dependent_event<InetSocketReadWriteEventBytes>(this, file);
}

virtual ReadWriteHTTPC::~ReadWriteHTTPC() {

}

CallResponse ReadWriteHTTPC::read_http(uint64_t) {
    read_op_ = true;
    parser_ = make_unique<http::response_parser<http::string_body>>();

    handle_read(0);
    return {"Read HTTP", true, OP_HINT_READ};
}

pair<GetDataInfo, unique_ptr<http::response_parser<http::string_body>>> ReadWriteHTTPC::get_data(uint64_t) {
    return {GetDataInfo {true}, std::move(parser_)};
}

CallResponse ReadWriteHTTPC::write_http(uint64_t, http::request<http::string_body> res) {
    read_op_ = false;
    http::request_serializer<http::string_body> sr{res};
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
    return {"HTTP Write", true, OP_HINT_WRITE};
}

void ReadWriteHTTPC::on_new_data(int, EventType event) {
    auto res = get<ChildTaskCompletion>(event);
    if (res.return_code <= 0) {
        AsyncHandler::self().finalize_current_task(true, res.return_code);
        return;
    }

    if (read_op_) {
        handle_read(res.return_code);
    } else {
        handle_write(res.return_code);
    }
}

void ReadWriteHTTPC::procedure_update(PUType, CallResponse) {

}

string ReadWriteHTTPC::get_info() const override {
    return "HTTP parser";
}

void ReadWriteHTTPC::handle_read(int res) {
    if (res > 0) {
        buffer_.commit(res);
    }

    bool done = try_parse();
    if (done) {
        AsyncHandler::self().finalize_current_task(false, 1);
        return;
    }

    arm_read();
}

void ReadWriteHTTPC::handle_write(int res) {
    if (res > 0) {
        write_buffer_.consume(res);
    }

    if (write_buffer_.size() > 0) {
        arm_write();
        return;
    }
    AsyncHandler::self().finalize_current_task(false, 1);
}

void ReadWriteHTTPC::arm_read() {
    // Prepare space in the flat_buffer for the next read
    auto mutable_buffer = buffer_.prepare(4096);
    if (tls_enabled_) {
        auto [taskid, success] = AsyncHandler::self().call_dependent_function<InetSocketTLSEvent>(
            this,
            0,
            &InetSocketTLSEvent::read,
            (char*)mutable_buffer.data(),
            4096,
            false
        );
        taskid_reader_ = taskid;
    } else {
        auto [taskid, success] = AsyncHandler::self().call_dependent_function<InetSocketReadWriteEventBytes>(
            this,
            0,
            &InetSocketReadWriteEventBytes::read,
            (char*)mutable_buffer.data(),
            4096,
            false
        );
        taskid_reader_ = taskid;
    }
}

void ReadWriteHTTPC::arm_write() {
    if (write_buffer_.size() == 0) return;
    if (tls_enabled_) {
        auto [taskid, success] = AsyncHandler::self().call_dependent_function<InetSocketTLSEvent>(
            this,
            0,
            &InetSocketTLSEvent::write,
            (char*)write_buffer_.data().data(),
            write_buffer_.size()
        );
        taskid_writer_ = taskid;
    } else {
        auto [taskid, success] = AsyncHandler::self().call_dependent_function<InetSocketReadWriteEventBytes>(
            this,
            0,
            &InetSocketReadWriteEventBytes::write,
            (char*)write_buffer_.data().data(),
            write_buffer_.size()
        );
        taskid_writer_ = taskid;

    }
}

bool ReadWriteHTTPC::try_parse() {
    boost::system::error_code ec;
    size_t consumed = parser_->put(buffer_.data(), ec);
    buffer_.consume(consumed);

    while ((!ec || ec == http::error::need_more) && !parser_->is_done() && buffer_.size()) {
        consumed = parser_->put(buffer_.data(), ec);
        buffer_.consume(consumed);
    }

    if (ec && ec != http::error::need_more) {
        // Treat parse errors as "done" so the caller can handle the error state in the parser
        return true;
    }

    return parser_->is_done();
}

