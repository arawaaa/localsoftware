#include "../common/inet_socket_read_write_event_http.hpp"
#include "../common/http_manager.hpp"
#include <fstream>
#include <thread>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common/defs.hpp"
#include "common/io_uring_manager.hpp"
#include "s40_client.hpp"

class AioLandingHTTP : public IoEvent {
public:
    AioLandingHTTP(std::unique_ptr<File> file, bool enable_tls, const HTTPManager& http_manager)
        : IoEvent(file->get()), http_manager_(http_manager)
    {
        IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventHTTP>(this, std::move(file), enable_tls);
    }

    CallResponse start() {
        auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
            this,
            &InetSocketReadWriteEventHTTP::read_http
        );
        return {"Server start up HTTP", true, OP_HINT_NETWORK};
    }

    void on_new_data(int op, EventType event) override {
        auto res = std::get<ChildTaskCompletion>(event);
        if (res.return_code < 0) {
            delete this;
            return;
        }
        IoUringManager::getInstance().consume_event(res.task_id);

        if (op_read_) {
            auto req = IoUringManager::getInstance().get_data<InetSocketReadWriteEventHTTP>(this, res.task_id).value()->get();

            if (req.method() == http::verb::get) {
                auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                    this,
                    &InetSocketReadWriteEventHTTP::write_http,
                    http_manager_.handle_request(req)
                );
            } else {
                delete this;
            }
        } else {
            // Continue reading
            auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                this,
                &InetSocketReadWriteEventHTTP::read_http
            );
        }
    }

    std::string get_info() const override { return "AioLandingHTTP FD " + std::to_string(fd_); }

private:
    bool op_read_;
    const HTTPManager& http_manager_;
};

class AioLandingAcceptEvent : public IoEvent {
public:
    AioLandingAcceptEvent(std::unique_ptr<File> server_file, bool use_tls, const std::string& base_dir)
        : IoEvent(std::move(server_file)), use_tls(use_tls), http_manager_(base_dir)
    {
        client_addr_len_ = sizeof(client_addr_);

        auto keep_alive_headers = std::vector<std::pair<http::field, std::string>>{
            {http::field::content_type, "text/plain"},
            {http::field::connection, "keep-alive"}
        };

        http_manager_.add_endpoint("/temperature", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok, 
                std::to_string(S40Client::getInstance().get_temperature()), req.version());
        });

        http_manager_.add_endpoint("/humidity", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok, 
                std::to_string(S40Client::getInstance().get_humidity()), req.version());
        });

        http_manager_.add_endpoint("/setpoint", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok, 
                std::to_string(S40Client::getInstance().get_setpoint()), req.version());
        });

        http_manager_.add_endpoint("/mode", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok, 
                S40Client::getInstance().get_mode(), req.version());
        });

        http_manager_.add_endpoint("/fan", [keep_alive_headers](const auto& req) {
            return HTTPManager::prepare_response(keep_alive_headers, http::status::ok, 
                std::to_string(S40Client::getInstance().get_fan()), req.version());
        });
    }

    void prepare_accept() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_accept, fd_, 
                         reinterpret_cast<struct sockaddr*>(&client_addr_), 
                         &client_addr_len_, 0);
    }

    void on_new_data(int op, EventType event) override {
        int res = std::get<IoUringResult>(event).res;
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
