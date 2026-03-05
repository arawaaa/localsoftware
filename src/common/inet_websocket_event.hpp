#pragma once

#include <memory>
#include <utility>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>

#include "common/inet_socket_read_write_event_bytes.hpp"
#include "io_event.hpp"
#include "io_uring_manager.hpp"
#include "inet_socket_tls_event.hpp"
#include "defs.hpp"

using namespace std;
namespace http = boost::beast::http;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class WebsocketEvent : public IoEvent {
public:
    WebsocketEvent(shared_ptr<File> file, bool tls_enabled = false)
        : IoEvent(file), tls_enabled_(tls_enabled)
    {
        /* Child Events must be moved from another event class, like the HTTP class */
    }

    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    CallResponse write_frame(char* buf, size_t len) {
        size_t size = 2 + (len > numeric_limits<uint8_t>::max() ? (len > numeric_limits<uint16_t>::max() ? ): 1);
        std::string frame;
        frame.push_back(static_cast<char>(0x82)); // FIN + Binary
        if (len <= 125) {
            frame.push_back(static_cast<char>(len));
        } else {
            frame.push_back(126);
            frame.push_back(static_cast<char>((len >> 8) & 0xFF));
            frame.push_back(static_cast<char>(len & 0xFF));
        }
        frame.append(reinterpret_cast<const char*>(data), len);

        arm_write();
        return {"Websocket Write", true, OP_HINT_WRITE};
    }

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    void on_new_data(int, EventType event) override {
        auto res = get<ChildTaskCompletion>(event);
        if (res.return_code <= 0) {
            IoUringManager::getInstance().finalize_current_task(true, res.return_code);
            IoUringManager::getInstance().consume_event(res.task_id);
            return;
        }

        handle_write(res);
        IoUringManager::getInstance().consume_event(res.task_id);
    }

    string get_info() const override {
        return "HTTP parser";
    }

protected:
    bool read_op_;
    uint64_t taskid_writer_, taskid_reader_;

    bool tls_enabled_;

    void handle_write(int res) {
        if (res > 0) {
            write_buffer_.consume(res);
        }

        if (write_buffer_.size() > 0) {
            arm_write();
            return;
        }
        IoUringManager::getInstance().finalize_current_task(false, 1);
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

