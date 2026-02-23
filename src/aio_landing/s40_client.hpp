#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <thread>
#include <chrono>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/json.hpp>
#include <openssl/ssl.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
namespace json = boost::json;
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

    int get_temperature() {
        std::lock_guard<std::mutex> lock(mutex_);
        return temperature_;
    }

    int get_setpoint() {
        std::lock_guard<std::mutex> lock(mutex_);
        return setpoint_;
    }

    int get_humidity() {
        std::lock_guard<std::mutex> lock(mutex_);
        return humidity_;
    }

    bool get_fan() {
        std::lock_guard<std::mutex> lock(mutex_);
        return fan_;
    }

    std::string get_mode() {
        std::lock_guard<std::mutex> lock(mutex_);
        return mode_;
    }

    bool is_data_valid() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_valid_;
    }

    ZoneData fetch_data() {
        ZoneData result;
        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx_);

            auto const results = resolver.resolve(host_, "443");
            beast::get_lowest_layer(stream).connect(results);

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host_.c_str())) {
                return result;
            }
            stream.handshake(ssl::stream_base::client);

            // 1. Connect
            {
                http::request<http::string_body> req{http::verb::post, "/Endpoints/" + client_id_ + "/Connect", 11};
                req.set(http::field::host, host_);
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                req.content_length(0);
                http::write(stream, req);

                http::response<http::string_body> res;
                beast::flat_buffer buffer;
                http::read(stream, buffer, res);
                if (res.result() != http::status::ok && res.result() != http::status::no_content) {
                    return result;
                }
            }

            // 2. RequestData
            {
                json::object payload;
                payload["MessageId"] = "1";
                payload["MessageType"] = "RequestData";
                payload["SenderId"] = client_id_;
                payload["TargetId"] = "LCC";
                json::object additional_params;
                additional_params["JSONPath"] = "1;/zones";
                payload["AdditionalParameters"] = additional_params;

                http::request<http::string_body> req{http::verb::post, "/Messages/RequestData", 11};
                req.set(http::field::host, host_);
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                req.set(http::field::content_type, "application/json");
                req.body() = json::serialize(payload);
                req.prepare_payload();
                http::write(stream, req);

                http::response<http::string_body> res;
                beast::flat_buffer buffer;
                http::read(stream, buffer, res);
                if (res.result() != http::status::ok && res.result() != http::status::no_content) {
                    return result;
                }
            }

            // 3. Poll Retrieve
            for (int i = 0; i < 60; ++i) { 
                http::request<http::string_body> req{http::verb::get, "/Messages/" + client_id_ + "/Retrieve", 11};
                req.set(http::field::host, host_);
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                http::write(stream, req);

                http::response<http::string_body> res;
                beast::flat_buffer buffer;
                http::read(stream, buffer, res);

                if (res.result() == http::status::ok) {
                    try {
                        json::value jv = json::parse(res.body());
                        if (jv.is_object()) {
                            auto const& obj = jv.as_object();
                            if (obj.contains("messages") && obj.at("messages").is_array()) {
                                for (auto const& msg : obj.at("messages").as_array()) {
                                    if (msg.is_object()) {
                                        auto const& msg_obj = msg.as_object();
                                        if (msg_obj.contains("Data") && msg_obj.at("Data").is_object()) {
                                            auto const& data_obj = msg_obj.at("Data").as_object();
                                            if (data_obj.contains("zones") && data_obj.at("zones").is_array()) {
                                                auto const& zones = data_obj.at("zones").as_array();
                                                if (!zones.empty()) {
                                                    auto const& z0 = zones[0].as_object();
                                                    if (z0.contains("status") && z0.at("status").is_object()) {
                                                        auto const& status = z0.at("status").as_object();
                                                        if (status.contains("temperature")) {
                                                            result.temperature = status.at("temperature").to_number<int>();
                                                            result.valid = true;
                                                        }
                                                        if (status.contains("humidity")) {
                                                            result.humidity = status.at("humidity").to_number<int>();
                                                            result.valid = true;
                                                        }
                                                        if (status.contains("fan")) {
                                                            result.fan = status.at("fan").as_bool();
                                                            result.valid = true;
                                                        }
                                                        std::cout << status << std::endl;
                                                        std::cout << status.contains("period") << ' ' << status.at("period").is_object() << std::endl;
                                                        if (status.contains("period") && status.at("period").is_object()) {
                                                            auto period_obj = status.at("period").as_object();
                                                            if (period_obj.contains("systemMode")) {
                                                                result.mode = period_obj.at("systemMode").as_string();
                                                                result.valid = true;
                                                            }
                                                            if (period_obj.contains("sp")) {
                                                                result.setpoint = period_obj.at("sp").to_number<int>();
                                                                result.valid = true;
                                                            }
                                                        }
                                                        if (result.valid) goto finished;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } catch (...) { }
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

        finished:
            // 4. Disconnect
            {
                http::request<http::string_body> req{http::verb::post, "/Endpoints/" + client_id_ + "/Disconnect", 11};
                req.set(http::field::host, host_);
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                req.content_length(0);
                http::write(stream, req);
            }
            beast::error_code ec;
            stream.shutdown(ec);
        } catch (...) { }

        return result;
    }

private:
    S40Client() : ctx_(ssl::context::tlsv12_client) {
        ctx_.set_verify_mode(ssl::verify_none);
        std::thread([this]() { loop(); }).detach();
    }

    void loop() {
        while (true) {
            std::cout << "[S40] Starting update cycle..." << std::endl;
            ZoneData data = fetch_data();

            if (data.valid) {
                std::lock_guard<std::mutex> lock(mutex_);
                temperature_ = data.temperature;
                setpoint_ = data.setpoint;
                humidity_ = data.humidity;
                fan_ = data.fan;
                mode_ = data.mode;
                data_valid_ = true;
                std::cout << "[S40] Updated: Temp=" << temperature_ << ", Hum=" << humidity_ << std::endl;
            } else {
                std::cerr << "[S40] Failed to update data." << std::endl;
            }

            // Refresh every 3 minutes
            std::this_thread::sleep_for(std::chrono::minutes(3));
        }
    }

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
