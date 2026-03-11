#pragma once

#include "common/io_event.hpp"
#include "common/io_uring_manager.hpp"
#include "common/inet_socket_read_write_event_http.hpp"
#include "common/http_manager.hpp"

class ReverseProxyServer : public IoEvent {
public:
    ReverseProxyServer(vector<shared_ptr<File>> file, const HTTPManager& http_manager, const string& proxyto, int port)
        : IoEvent(file), http_manager_(http_manager)
    {
        // TLS always for reverse proxies
        IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventHTTP>(this, file, true);
    }

    CallResponse start(uint64_t) {
        IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
            this,
            0,
            &InetSocketReadWriteEventHTTP::read_http
        );
        op_read_ = true;
        return {"Server start up HTTP", true, OP_HINT_NETWORK};
    }

    void on_new_data(int, EventType event) override {
        auto res = get<ChildTaskCompletion>(event);
        if (res.return_code <= 0) {
            IoUringManager::getInstance().finalize_current_task(true, -1);
            return;
        }

        if (op_read_) {
            auto req = IoUringManager::getInstance().get_data<InetSocketReadWriteEventHTTP>(this, 0, res.task_id).value()->get();

            if (req.method() == http::verb::get) {
                IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                    this,
                    0,
                    &InetSocketReadWriteEventHTTP::write_http,
                    http_manager_.handle_request(req)
                );
                op_read_ = false;
            } else {
                IoUringManager::getInstance().finalize_current_task(true, -1);
            }
        } else {
            // Continue reading
            IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventHTTP>(
                this,
                0,
                &InetSocketReadWriteEventHTTP::read_http
            );
            op_read_ = true;
        }
        IoUringManager::getInstance().consume_event(res.task_id);
    }

    string get_info() const override { return "ReverseProxyServer FD " + to_string(files_[0]->get()); }

private:
    bool op_read_;
    const HTTPManager& http_manager_;
};
