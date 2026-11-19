#pragma once

#include <coroutine>
#include <boost/beast/http/string_body.hpp>

#include "event.h"
#include "defs.h"
#include "http_manager.h"
#include "readwrite_http.h"
#include "coroutine/pinned.h"

using namespace std;

class AioLandingHTTP : public Event {
    template<class... Ts>
    struct overloaded : Ts... { using Ts::operator()...; };
public:
    AioLandingHTTP(vector<shared_ptr<File>> file, bool enable_tls, const HTTPManager* http_manager);

    pinned<int> start();

private:
    void handle_request(boost::beast::http::request_parser<boost::beast::http::string_body>::value_type& req);

    void handle_response();

    ReadWriteHTTP http_adaptor_;
    static constexpr __kernel_timespec ts = {.tv_sec = 180, .tv_nsec=0};
    bool op_read_;
    const HTTPManager* http_manager_;
    uint64_t timer_ = 0;
    int return_code_;
};
