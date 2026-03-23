#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common/defs.cpp"
#include "common/io_uring_manager.cpp"
#include "common/inet_socket_read_write_event_http.cpp"
#include "common/http_manager.cpp"

using namespace std;

class AioLandingHTTP : public IoEvent {
public:
    AioLandingHTTP(vector<shared_ptr<File>> file, bool enable_tls, const HTTPManager& http_manager)
        : IoEvent(file), http_manager_(http_manager)
    {
        IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventHTTP>(this, file, enable_tls);
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

    string get_info() const override { return "AioLandingHTTP FD " + to_string(files_[0]->get()); }

private:
    bool op_read_;
    const HTTPManager& http_manager_;
};
