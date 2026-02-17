#include "../common/inet_socket_read_write_event_http.hpp"
#include "../common/http_manager.hpp"
#include <fstream>
#include <thread>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "s40_client.hpp"

class AioLandingHTTP : public InetSocketReadWriteEventHTTP {
public:
    AioLandingHTTP(std::unique_ptr<File> file, bool enable_tls, const HTTPManager& http_manager)
        : InetSocketReadWriteEventHTTP(std::move(file), enable_tls), http_manager_(http_manager) {}

    void post(int id, int res) override {
        std::cout << res << ' ' << id << std::endl;
        if (res <= 0) {
            delete this;
            return;
        }

        if (id == ID_READ) {
            auto req = parser_->get();
            if (req.method() == http::verb::get) {
                if (req.target() == "/temperature") {
                    http::response<http::string_body> resp{http::status::ok, req.version()};
                    resp.set(http::field::content_type, "text/plain");
                    resp.set(http::field::connection, "keep-alive");
                    resp.body() = std::to_string(S40Client::getInstance().get_temperature());
                    resp.prepare_payload();
                    write_http(std::move(resp));
                    return;
                } else if (req.target() == "/humidity") {
                    http::response<http::string_body> resp{http::status::ok, req.version()};
                    resp.set(http::field::content_type, "text/plain");
                    resp.set(http::field::connection, "keep-alive");
                    resp.body() = std::to_string(S40Client::getInstance().get_humidity());
                    resp.prepare_payload();
                    write_http(std::move(resp));
                    return;
                }

                // Use HTTPManager for other paths or static files
                write_http(http_manager_.handle_request(req));
            } else {
                delete this;
            }
        } else if (id == ID_WRITE) {
            // Continue reading
            read_http();
        }
    }

    std::string get_info() const override { return "AioLandingHTTP FD " + std::to_string(fd_); }

private:
    const HTTPManager& http_manager_;
};

class AioLandingAcceptEvent : public IoEvent {
public:
    AioLandingAcceptEvent(std::unique_ptr<File> server_file, bool use_tls, const std::string& base_dir)
        : IoEvent(std::move(server_file)), use_tls(use_tls), http_manager_(base_dir)
    {
        client_addr_len_ = sizeof(client_addr_);
    }

    void prepare_accept() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_accept, fd_, 
                         reinterpret_cast<struct sockaddr*>(&client_addr_), 
                         &client_addr_len_, 0);
    }

    void post(int id, int res) override {
        if (res >= 0) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
            std::cout << "[LANDING" << (use_tls ? " TLS" : "") <<"] Accept with " << ip_str << ":" << client_addr_.sin_port << std::endl;
            auto client_file = std::make_unique<File>(res);
            auto* http_ev = new AioLandingHTTP(std::move(client_file), use_tls, http_manager_);
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
    HTTPManager http_manager_;
};
