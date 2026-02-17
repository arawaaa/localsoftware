#pragma once

#include "../common/inet_socket_read_write_event_http.hpp"
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>

class AioLandingHTTP : public InetSocketReadWriteEventHTTP {
public:
    AioLandingHTTP(std::unique_ptr<File> file, bool enable_tls)
        : InetSocketReadWriteEventHTTP(std::move(file), enable_tls) {}

    void post(int id, int res) override {
        if (res <= 0) {
            delete this;
            return;
        }

        if (id == ID_READ) {
            auto req = parser_->get();
            if (req.method() == http::verb::get) {
                std::string path;
                if (req.target() == "/") {
                    path = "/srv/landing.html";
                } else if (req.target() == "/bandwidth") {
                    path = "/srv/dashboard.html";
                }

                if (!path.empty()) {
                    std::ifstream ifs(path);
                    if (ifs) {
                        std::string content((std::istreambuf_iterator<char>(ifs)),
                                            (std::istreambuf_iterator<char>()));
                        
                        http::response<http::string_body> resp{http::status::ok, req.version()};
                        resp.set(http::field::content_type, "text/html");
                        resp.set(http::field::connection, "close");
                        resp.body() = std::move(content);
                        resp.prepare_payload();
                        
                        write_http(std::move(resp));
                        return;
                    }
                }
                
                // Not handled or not found
                delete this;
            } else {
                delete this;
            }
        } else if (id == ID_WRITE) {
            // Write finished, and we set Connection: close
            delete this;
        }
    }

    std::string get_info() const override { return "AioLandingHTTP FD " + std::to_string(fd_); }
};

class AioLandingAcceptEvent : public IoEvent {
public:
    AioLandingAcceptEvent(std::unique_ptr<File> server_file, bool use_tls)
        : IoEvent(std::move(server_file)), use_tls(use_tls) {
        client_addr_len_ = sizeof(client_addr_);
    }

    void prepare_accept() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_accept, fd_, 
                         reinterpret_cast<struct sockaddr*>(&client_addr_), 
                         &client_addr_len_, 0);
    }

    void post(int id, int res) override {
        if (res >= 0) {
            std::cout << "P80 Accept New Connection" << std::endl;
            auto client_file = std::make_unique<File>(res);
            auto* http_ev = new AioLandingHTTP(std::move(client_file), use_tls);
            http_ev->read_http();
        }
        // Re-arm immediately
        prepare_accept();
    }

    std::string get_info() const override { return "AioLandingAcceptEvent FD " + std::to_string(fd_); }

private:
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
    bool use_tls;
};
