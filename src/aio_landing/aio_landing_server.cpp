#pragma once

#include <boost/beast/http/string_body.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>

#include "common/defs.cpp"
#include "common/io_uring_manager.cpp"
#include "common/inet_socket_read_write_event_http.cpp"
#include "common/http_manager.cpp"

using namespace std;

class AioLandingHTTP : public IoEvent {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    AioLandingHTTP(vector<shared_ptr<File>> file, bool enable_tls, const HTTPManager& http_manager)
        : IoEvent(file), http_manager_(http_manager)
    {
        IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventHTTP>(this, file, enable_tls);
    }

    CallResponse start(uint64_t) {
        IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
            this,
            0,
            &InetSocketReadWriteEventHTTP::read_http
        );
        op_read_ = true;
        timer_ = IoUringManager::getInstance().set_timer(ts);
        return {"Server start up HTTP", true, OP_HINT_NETWORK};
    }

    void on_new_data(int, EventType event) override {
        visit(overloaded {
            [this](ChildTaskCompletion& child) {
                if (child.return_code <= 0) {
                    IoUringManager::getInstance().finalize_current_task(true, child.return_code);
                }

                if (op_read_) {
                    IoUringManager::getInstance().cancel_timer(timer_);
                    auto req = IoUringManager::getInstance().get_data<InetSocketReadWriteEventHTTP>(this, 0, child.task_id).value()->get();
                    handle_request(req);
                } else {
                    timer_ = IoUringManager::getInstance().set_timer(ts);
                    handle_response();
                }
            },
            [](Timeout&) {
                IoUringManager::getInstance().finalize_current_task(true, -1);
            },
            [](auto&) {}
        }, event);


    }

    string get_info() const override { return "AioLandingHTTP FD " + to_string(files_[0]->get()); }

private:
    void handle_request(boost::beast::http::request_parser<boost::beast::http::string_body>::value_type& req) {
        IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
            this,
            0,
            &InetSocketReadWriteEventHTTP::write_http,
            http_manager_.handle_request(req)
        );
        op_read_ = false;
    }

    void handle_response() {
        IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
            this,
            0,
            &InetSocketReadWriteEventHTTP::read_http
        );
        op_read_ = true;
    }

    const __kernel_timespec ts = {.tv_sec = 180, .tv_nsec=0};
    bool op_read_;
    const HTTPManager& http_manager_;
    uint64_t timer_ = 0;
};
