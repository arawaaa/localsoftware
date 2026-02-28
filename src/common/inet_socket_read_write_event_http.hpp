#pragma once

#include "common/inet_socket_read_write_event_bytes.hpp"
#include "io_event.hpp"
#include "io_uring_manager.hpp"
#include "inet_socket_tls_event.hpp"
#include "defs.hpp"
#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <openssl/bio.h>
#include <utility>
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace http = boost::beast::http;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class InetSocketReadWriteEventHTTP : public IoEvent {
public:
    InetSocketReadWriteEventHTTP(std::unique_ptr<File> file, bool use_ssl = false)
        : IoEvent(file->get()), tls_enabled_(use_ssl)
    {
        if (tls_enabled_)
            IoUringManager::getInstance().initialize_dependent_event<InetSocketTLSEvent>(this, std::move(file));
        else
            IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventBytes>(this, std::move(file));
    }

    virtual ~InetSocketReadWriteEventHTTP() {

    }

    /**
     * @brief Initiates or restarts the HTTP reading process.
     * Reassigns the parser for a new request and processes any leftover data.
     */
    CallResponse read_http(uint64_t taskid) {
        read_op_ = true;
        parser_ = std::make_unique<http::request_parser<http::string_body>>();
        if (buffer_.size() > 0) {
            if (try_parse()) {
                handle_read(ID_READ, 0);
            }
        }
        arm_read();
        return {"Read HTTP", true, OP_HINT_READ};
    }

    /**
     * @brief Returns the completed parser by moving it out.
     */
    std::pair<GetDataInfo, std::unique_ptr<http::request_parser<http::string_body>>> get_data(uint64_t taskid) {
        return {GetDataInfo {true}, std::move(parser_)};
    }

    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    CallResponse write_http(uint64_t taskid, http::response<http::string_body> res) {
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
        return {"HTTP Write", true, OP_HINT_WRITE};
    }

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    void on_new_data(int op, EventType event) override {
        auto res = std::get<ChildTaskCompletion>(event);
        if (res.return_code <= 0) {
            IoUringManager::getInstance().finalize_current_task(true, res.return_code);
            return;
        }

        if (read_op_) {
            handle_read(op, res.return_code);
        } else {
            handle_write(op, res.return_code);
        }
    }

    std::string get_info() const override {
        return "HTTP parser";
    }

protected:
    bool read_op_;
    uint64_t taskid_writer_, taskid_reader_;
    std::unique_ptr<http::request_parser<http::string_body>> parser_;
    boost::beast::flat_buffer buffer_;
    boost::beast::flat_buffer write_buffer_;

    bool tls_enabled_;

    void handle_read(int id, int res) {
        if (res > 0) {
            buffer_.commit(res);
        }

        bool done = try_parse();
        if (done) {
            IoUringManager::getInstance().finalize_current_task(false, 1);
            return;
        }

        arm_read();
    }

    void handle_write(int id, int res) {
        if (res > 0) {
            write_buffer_.consume(res);
        }

        if (write_buffer_.size() > 0) {
            arm_write();
            return;
        }
        IoUringManager::getInstance().finalize_current_task(false, 1);
    }

    void arm_read() {
        // Prepare space in the flat_buffer for the next read
        auto mutable_buffer = buffer_.prepare(4096);
        if (tls_enabled_) {
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketTLSEvent>(
                this,
                0,
                &InetSocketTLSEvent::read,
                (char*)mutable_buffer.data(),
                4096,
                false
            );
            taskid_reader_ = taskid;
        } else {
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventBytes>(
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

    void arm_write() {
        if (write_buffer_.size() == 0) return;
        if (tls_enabled_) {
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketTLSEvent>(
                this,
                0,
                &InetSocketTLSEvent::write,
                (char*)write_buffer_.data().data(),
                write_buffer_.size()
            );
            taskid_writer_ = taskid;
        } else {
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventBytes>(
                this,
                0,
                &InetSocketReadWriteEventBytes::write,
                (char*)write_buffer_.data().data(),
                write_buffer_.size()
            );
            taskid_writer_ = taskid;

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

