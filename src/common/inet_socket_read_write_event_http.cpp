#pragma once

#include <memory>
#include <utility>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/optional/optional_io.hpp>

#include "inet_socket_read_write_event_bytes.cpp"
#include "io_event.cpp"
#include "inet_socket_tls_event.cpp"
#include "defs.cpp"

using namespace std;
namespace http = boost::beast::http;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class InetSocketReadWriteEventHTTP : public Event {
public:
    InetSocketReadWriteEventHTTP(vector<shared_ptr<File>> file, bool use_ssl = false)
        : Event(file), tls_enabled_(use_ssl)
    {

    }

    void construct_with_global() override {
        if (tls_enabled_)
            i<InetSocketTLSEvent>(files_, true);
        else
            i<InetSocketReadWriteEventBytes>(files_);
    }

    /**
     * @brief Initiates or restarts the HTTP reading process.
     * Reassigns the parser for a new request and processes any leftover data.
     */
    CallResponse read_http(uint64_t) {
        read_op_ = true;
        parser_ = make_unique<http::request_parser<http::string_body>>();

        arm_read();
        return {"Read HTTP", true, nullopt, OP_HINT_READ};
    }

    /**
     * @brief Returns the completed parser by moving it out.
     */
    unique_ptr<http::request_parser<http::string_body>> get_data(uint64_t) {
        return std::move(parser_);
    }

    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    CallResponse write_http(uint64_t, http::response<http::string_body> res) {
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

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    optional<pair<bool, int>> on_yield(EventType event) override {
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

    string get_info() const override {
        return "HTTP parser";
    }

protected:
    bool read_op_;
    uint64_t taskid_writer_, taskid_reader_;
    unique_ptr<http::request_parser<http::string_body>> parser_;
    boost::beast::flat_buffer buffer_;
    boost::beast::flat_buffer write_buffer_;

    bool tls_enabled_;

    /**
     * @brief Handle read operation, and commit to buffer if new bytes present
     */
    optional<pair<bool, int>> handle_read(int res) {
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

    /**
     * @brief Write out entire buffer
     */
    optional<pair<bool, int>> handle_write(int res) {
        if (res > 0) {
            write_buffer_.consume(res);
        }

        if (write_buffer_.size() > 0) {
            arm_write();
            return nullopt;
        }
        return pair{false, 1};
    }

    void arm_read() {
        // Prepare space in the flat_buffer for the next read
        auto mutable_buffer = buffer_.prepare(4096);
        if (tls_enabled_) {
            uint64_t taskid = c(&InetSocketTLSEvent::read, (char*)mutable_buffer.data(), 4096, false);
            taskid_reader_ = taskid;
        } else {
            uint64_t taskid = c(&InetSocketReadWriteEventBytes::read, (char*)mutable_buffer.data(), 4096, false);
            taskid_reader_ = taskid;
        }
    }

    void arm_write() {
        if (write_buffer_.size() == 0) return;
        if (tls_enabled_) {
            uint64_t taskid = c(&InetSocketTLSEvent::write, (char*)write_buffer_.data().data(), write_buffer_.size());
            taskid_writer_ = taskid;
        } else {
            uint64_t taskid = c(&InetSocketReadWriteEventBytes::write, (char*)write_buffer_.data().data(), write_buffer_.size());
            taskid_writer_ = taskid;

        }
    }

    /**
     * @brief Parse request incrementally, signalling to requeue if incomplete
     */
    bool try_parse() {
        boost::system::error_code ec;
        size_t consumed = parser_->put(buffer_.data(), ec);
        buffer_.consume(consumed);

        while ((!ec || ec == http::error::need_more) && !parser_->is_done() && (consumed && buffer_.size())) {
            consumed = parser_->put(buffer_.data(), ec);
            buffer_.consume(consumed);
        }

        if (ec && ec != http::error::need_more) {
            // Treat parse errors as "done" so the caller can handle the error state in the parser
            return true;
        }

        return parser_->is_done();
    }
};

