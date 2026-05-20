#pragma once

#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>
#include <optional>

#include "listener.cpp"
#include "common/io_event.cpp"
#include "common/defs.cpp"

using namespace std;

class TestClass : public Event {
public:
    TestClass(vector<shared_ptr<File>> server_file): Event(server_file) {
    }

    CallResponse init(uint64_t taskid) {
        taskid_ = taskid;
        queue_accept(0);
        queue_accept(1);
        return {"HTTP accepter", true, nullopt, OP_HINT_NETWORK};
    }

    void construct_with_global() override {
    }

    optional<pair<bool, int>> on_yield(EventType event) override {
        if (holds_alternative<ChildTaskCompletion>(event)) {
            std::cout << "Connection close" << std::endl;
            auto res = get<ChildTaskCompletion>(event);
        } else {
            auto uri_res = get<IoUringResult>(event);

            if (uri_res.res >= 0) {
                char ip_str[INET6_ADDRSTRLEN];
                if (!uri_res.op)
                    inet_ntop(AF_INET, &(client_addr_.sin_addr), ip_str, INET_ADDRSTRLEN);
                else
                    inet_ntop(AF_INET6, &(client_addr6_.sin6_addr), ip_str, INET6_ADDRSTRLEN);

                close(uri_res.res);
                cout << "Had new connection" << endl;

            }
            // Re-arm immediately
            queue_accept(uri_res.op);
        }
        return nullopt;
    }

    string get_info() const override { return "Test"; }

private:
    void queue_accept(int op) {
        c(op, &io_uring_prep_accept, files_[op]->get(),
            op == 0 ? reinterpret_cast<struct sockaddr*>(&client_addr_)
                : reinterpret_cast<struct sockaddr*>(&client_addr6_),
            op == 0 ? &client_addr_len_
                : &client_addr6_len_, 0);
    }

    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
    struct sockaddr_in6 client_addr6_{};
    socklen_t client_addr6_len_;
    uint64_t taskid_;
};
