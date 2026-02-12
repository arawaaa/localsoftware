#pragma once

#include "io_event.hpp"
#include "io_uring_manager.hpp"
#include "defs.hpp"
#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <utility>

namespace http = boost::beast::http;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class InetSocketReadWriteEventHTTP : public IoEvent {
public:
    using IoEvent::IoEvent;

    /**
     * @brief Initiates or restarts the HTTP reading process.
     * Reassigns the parser for a new request and processes any leftover data.
     */
    void read_http() {
        parser_ = std::make_unique<http::request_parser<http::string_body>>();
        if (buffer_.size() > 0) {
            if (try_parse()) {
                // Already have a full request from leftover data.
                // Trigger a NOP to entry the event loop cycle and call post().
                IoUringManager::getInstance().cache_call(this, ID_READ, io_uring_prep_nop);
                return;
            }
        }
        arm_read();
    }

    /**
     * @brief Returns the completed parser by moving it out.
     */
    std::unique_ptr<http::request_parser<http::string_body>> get_parser() {
        return std::move(parser_);
    }

    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    void write_http(http::response<http::string_body> res) {
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
    }

    std::pair<bool, int> abstract_event_success(int id, int res) override {
        if (res < 0) return {false, res}; // Error
        
        if (id == ID_READ) {
            if (res > 0) {
                buffer_.commit(res);
            }
            
            bool done = try_parse();
            if (done) {
                return {true, res};
            }
            
            arm_read();
            return {false, res};
        } else if (id == ID_WRITE) {
            if (res > 0) {
                write_buffer_.consume(res);
            }

            if (write_buffer_.size() > 0) {
                arm_write();
                return {false, res};
            }
            return {true, res};
        }
        
        return {true, res};
    }

protected:
    std::unique_ptr<http::request_parser<http::string_body>> parser_;
    boost::beast::flat_buffer buffer_;
    boost::beast::flat_buffer write_buffer_;

    void arm_read() {
        // Prepare space in the flat_buffer for the next read
        auto mutable_buffer = buffer_.prepare(2048);
        IoUringManager::getInstance().cache_call(this, ID_READ, io_uring_prep_recv, fd_, 
                                                 mutable_buffer.data(), mutable_buffer.size(), 0);
    }

    void arm_write() {
        if (write_buffer_.size() > 0) {
            IoUringManager::getInstance().cache_call(this, ID_WRITE, io_uring_prep_send, fd_, 
                                                     write_buffer_.data().data(), write_buffer_.size(), MSG_NOSIGNAL);
        }
    }

    bool try_parse() {
        boost::system::error_code ec;
        size_t consumed = parser_->put(buffer_.data(), ec);
        buffer_.consume(consumed);

        if (ec && ec != http::error::need_more) {
            // Treat parse errors as "done" so the caller can handle the error state in the parser
            return true;
        }

        return parser_->is_done();
    }
};

/**
 * @brief Identical to InetSocketReadWriteEventHTTP, provided for naming consistency.
 */
class InetSocketReadWriteEventBytesHTTP : public InetSocketReadWriteEventHTTP {
public:
    using InetSocketReadWriteEventHTTP::InetSocketReadWriteEventHTTP;
};
