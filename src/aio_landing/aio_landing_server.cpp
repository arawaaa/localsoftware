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

class AioLandingHTTP : public Event {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    AioLandingHTTP(vector<shared_ptr<File>> file, bool enable_tls, const HTTPManager* http_manager)
        : Event(file), http_manager_(http_manager)
    {
        i<InetSocketReadWriteEventHTTP>(files_, enable_tls);
    }

    CallResponse start(uint64_t) {
        c(&InetSocketReadWriteEventHTTP::read_http);
        op_read_ = true;
        timer_ = timer(ts);
        return {"Server start up HTTP", true, nullopt, OP_HINT_NETWORK};
    }

    optional<pair<bool, int>> on_yield(EventType event) override {
        return visit(overloaded {
            [this](ChildTaskCompletion& child) -> optional<pair<bool, int>> {
                if (child.return_code <= 0) {
                    return pair{true, child.return_code};
                }

                if (op_read_) {
                    cancel_timer(timer_);
                    auto req = direct_access(0, &InetSocketReadWriteEventHTTP::get_data, child.task_id).value()->get();
                    handle_request(req);
                } else {
                    timer_ = timer(ts);
                    handle_response();
                }
                return nullopt;
            },
            [](Timeout&) -> optional<pair<bool, int>> {
                return pair{true, -1};
            },
            [](auto&) -> optional<pair<bool, int>> {
                return nullopt;
            }
        }, event);
    }

    string get_info() const override { return "AioLandingHTTP FD " + to_string(files_[0]->get()); }

private:
    void handle_request(boost::beast::http::request_parser<boost::beast::http::string_body>::value_type& req) {
        c(&InetSocketReadWriteEventHTTP::write_http, http_manager_->handle_request(req));
        op_read_ = false;
    }

    void handle_response() {
        c(&InetSocketReadWriteEventHTTP::read_http);
        op_read_ = true;
    }

    const __kernel_timespec ts = {.tv_sec = 180, .tv_nsec=0};
    bool op_read_;
    const HTTPManager* http_manager_;
    uint64_t timer_ = 0;
};
