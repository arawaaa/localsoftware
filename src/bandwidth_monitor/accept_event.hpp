#pragma once

#include "../common/io_event.hpp"
#include "../common/io_uring_manager.hpp"
#include "bandwidth_data_read_event.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <cstring>

class BandwidthMonitorAcceptEvent : public IoEvent {
public:
    // Takes ownership of the listening server socket file descriptor
    BandwidthMonitorAcceptEvent(std::unique_ptr<File> server_file) 
        : IoEvent(std::move(server_file)) {
        client_addr_len_ = sizeof(client_addr_);
    }

    void prepare_accept() {
        IoUringManager::getInstance().cache_call(this, ID_DEFAULT, io_uring_prep_accept, fd_, 
                         reinterpret_cast<struct sockaddr*>(&client_addr_), 
                         &client_addr_len_, 0);
    }

    void post(int id, int res) override {
        if (res < 0) {
            // Re-arm to keep accepting even on failure
            prepare_accept();
            return;
        }

        // Wrap the new client socket in a File object
        auto client_file = std::make_unique<File>(res);
        std::cout << "[ACCEPT] New connection on FD " << res << std::endl;

        // Create and enqueue the ReadEvent for the new client, transferring ownership of the FD
        auto* read_ev = new BandwidthDataReadEvent(std::move(client_file));
        read_ev->prepare_read();

        // Re-arm the accept event to listen for the next connection
        prepare_accept();
    }

    std::string get_info() const override {
        return "BandwidthMonitorAcceptEvent on FD " + std::to_string(fd_);
    }

private:
    struct sockaddr_in client_addr_{};
    socklen_t client_addr_len_;
};
