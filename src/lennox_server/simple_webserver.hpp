#pragma once

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <fstream>
#include <sstream>
#include "s40_client.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Global state for simple data sharing
std::mutex g_zone_mutex;
double g_temperature = 0.0;
double g_humidity = 0.0;
bool g_data_valid = false;

class SimpleWebserver {
public:
    void run(unsigned short port) {
        try {
            net::io_context ioc{1};
            tcp::acceptor acceptor{ioc, {tcp::v4(), port}};
            
            std::cout << "[HTTP] Serial webserver listening on port " << port << std::endl;

            S40Client s40;

            for (;;) {
                // 1. Accept Connection (Blocking)
                tcp::socket socket{ioc};
                acceptor.accept(socket);

                beast::error_code ec;
                beast::flat_buffer buffer;

                // 2. Read Request
                http::request<http::string_body> req;
                http::read(socket, buffer, req, ec);

                if (ec == http::error::end_of_stream) {
                    socket.shutdown(tcp::socket::shutdown_send, ec);
                    continue;
                }
                if (ec) {
                    std::cerr << "[HTTP] Read error: " << ec.message() << std::endl;
                    continue;
                }

                // 3. Handle Request
                http::response<http::string_body> res;
                res.version(req.version());
                res.keep_alive(false);

                if (req.method() == http::verb::get) {
                    if (req.target() == "/") {
                        res.result(http::status::ok);
                        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                        res.set(http::field::content_type, "text/html");
                        
                        // Read monitoringindex.html
                        std::ifstream f("/srv/monitoringindex.html");
                        if (f) {
                            std::stringstream ss;
                            ss << f.rdbuf();
                            res.body() = ss.str();
                        } else {
                            res.body() = "<h1>Index not found</h1>";
                        }
                    } else if (req.target() == "/temperature") {
                        res.result(http::status::ok);
                        res.set(http::field::content_type, "text/plain");
                        std::lock_guard<std::mutex> lock(g_zone_mutex);
                        res.body() = g_data_valid ? std::to_string(g_temperature) : "N/A";
                    } else if (req.target() == "/humidity") {
                        res.result(http::status::ok);
                        res.set(http::field::content_type, "text/plain");
                        std::lock_guard<std::mutex> lock(g_zone_mutex);
                        res.body() = g_data_valid ? std::to_string(g_humidity) : "N/A";
                    } else {
                        res.result(http::status::not_found);
                        res.set(http::field::content_type, "text/plain");
                        res.body() = "Not Found";
                    }
                } else {
                    res.result(http::status::bad_request);
                    res.body() = "Bad Request";
                }

                res.prepare_payload();

                // 4. Send Response
                http::write(socket, res, ec);
                socket.shutdown(tcp::socket::shutdown_send, ec);
                socket.close(); // Close immediately

                // 5. Post-Processing: Update S40 Data
                // The user requested: "After each request is served, I want you to query the zones endpoint"
                // This blocks the server from accepting new connections until complete (Serial-only).
                std::cout << "[HTTP] Request served. Updating S40 data..." << std::endl;
                S40Client::ZoneData data = s40.fetch_data();
                
                if (data.valid) {
                    std::lock_guard<std::mutex> lock(g_zone_mutex);
                    g_temperature = data.temperature;
                    g_humidity = data.humidity;
                    g_data_valid = true;
                    std::cout << "[S40] Updated: Temp=" << g_temperature << ", Hum=" << g_humidity << std::endl;
                } else {
                    std::cerr << "[S40] Failed to update data." << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[HTTP] Fatal error: " << e.what() << std::endl;
        }
    }
};
