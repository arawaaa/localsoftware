#pragma once

#include "io_event.hpp"
#include "bandwidth_data_read_event.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>

class BandwidthMonitorAcceptEvent : public IoEvent {
public:
    // Takes ownership of the listening server socket file descriptor
    BandwidthMonitorAcceptEvent(std::unique_ptr<File> server_file, struct io_uring* ring) 
        : IoEvent(std::move(server_file)), ring_(ring) {
        client_addr_len_ = sizeof(client_addr_);
    }

    void run(struct io_uring_sqe* sqe) override {
        io_uring_prep_accept(sqe, fd_, 
                             reinterpret_cast<struct sockaddr*>(&client_addr_), 
                             &client_addr_len_, 0);
    }

    void post(int res) override {
        if (res < 0) {
            // Re-arm to keep accepting even on failure
            this->on(ring_);
            return;
        }

        // Wrap the new client socket in a File object
        auto client_file = std::make_unique<File>(res);

        // Create and enqueue the ReadEvent for the new client, transferring ownership of the FD
        auto* read_ev = new BandwidthDataReadEvent(std::move(client_file), ring_);
        read_ev->on(ring_);

        // Re-arm the accept event to listen for the next connection
        this->on(ring_);
    }

    std::string get_info() const override {
        return "BandwidthMonitorAcceptEvent on FD " + std::to_string(fd_);
    }

private:
    struct io_uring* ring_;
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
};
