#pragma once

#include <string>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/json.hpp>

#include <openssl/ssl.h>

namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

class S40Client {
public:
    static S40Client& getInstance() {
        static S40Client instance;
        return instance;
    }

    S40Client(const S40Client&) = delete;
    S40Client& operator=(const S40Client&) = delete;

    struct ZoneData {
        int temperature = 0;
        int setpoint = 0;
        int humidity = 0;
        bool fan = false;
        std::string mode;
        bool valid = false;
    };

    int get_temperature();

    int get_setpoint();

    int get_humidity();

    bool get_fan();

    std::string get_mode();

    bool is_data_valid();

    ZoneData fetch_data();

private:
    S40Client();

    void loop();

    std::string host_ = "Lennox-S40-BT24E01839.local";
    std::string client_id_ = "simple_zone_requester_cpp";
    ssl::context ctx_;
    std::mutex mutex_;
    int temperature_ = 0;
    int setpoint_ = 0;
    int humidity_ = 0;
    bool fan_ = false;
    std::string mode_;
    bool data_valid_ = false;
};
