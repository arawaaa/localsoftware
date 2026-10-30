#pragma once

#include <boost/beast/http/string_body.hpp>

#include "event.h"
#include "defs.h"
#include "http_manager.h"

using namespace std;

class AioLandingHTTP : public Event {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    AioLandingHTTP(vector<shared_ptr<File>> file, bool enable_tls, const HTTPManager* http_manager);

    CallResponse start(uint64_t);

    optional<pair<bool, int>> on_yield(EventType event) override;

    string get_info() const override;

private:
    void handle_request(boost::beast::http::request_parser<boost::beast::http::string_body>::value_type& req);

    void handle_response();

    const __kernel_timespec ts = {.tv_sec = 180, .tv_nsec=0};
    bool op_read_;
    const HTTPManager* http_manager_;
    uint64_t timer_ = 0;
};
