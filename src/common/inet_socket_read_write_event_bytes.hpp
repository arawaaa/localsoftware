#pragma once

#include <cstdint>
#include <iostream>
#include "io_event.hpp"
#include "io_uring_manager.hpp"
#include "defs.hpp"
#include <utility>

class InetSocketReadWriteEventBytes : public IoEvent {
public:
    InetSocketReadWriteEventBytes(std::unique_ptr<File> file)
        : IoEvent(std::move(file)) {}

    CallResponse read(uint64_t taskid, void* buf, size_t len, bool read_all = true) {
        sticky_read_ = read_all;
        read_buffer_ = buf;
        read_bytes_left_ = len;
        read_total_processed_ = 0;
        IoUringManager::getInstance().cache_call(this, ID_READ, io_uring_prep_recv, fd_, read_buffer_, read_bytes_left_, 0);
        return {"Read len bytes into buf", true, OpHint::OP_HINT_READ | OpHint::OP_HINT_NETWORK};
    }

    CallResponse write(uint64_t taskid, void* buf, size_t len) {
        write_buffer_ = buf;
        write_bytes_left_ = len;
        write_total_processed_ = 0;
        IoUringManager::getInstance().cache_call(this, ID_WRITE, io_uring_prep_send, fd_, write_buffer_, write_bytes_left_, MSG_NOSIGNAL);
        return {"Write len bytes from buf", true, OpHint::OP_HINT_WRITE | OpHint::OP_HINT_NETWORK};
    }

    std::pair<GetDataInfo, void*> get_data(uint64_t id) {
        if (id == ID_READ) {
            return {{true}, read_buffer_};
        }
        return {{false}, nullptr};
    }

    void on_new_data(int op, EventType event) override {
        int res = std::get<IoUringResult>(event).res;
        if (res <= 0) {
            IoUringManager::getInstance().finalize_current_task(true, res);
            return;
        }
        
        if (op == ID_READ) {
            prepare_read(res);
        } else if (op == ID_WRITE) {
            prepare_write(res);
        }
    }

    std::string get_info() const override {
        return "inet socket readwriter FD " + std::to_string(file_->get());
    }

private:
    bool sticky_read_ = false;

    void prepare_read(int res) {
        read_bytes_left_ -= res;
        read_total_processed_ += res;
        if (sticky_read_ && read_bytes_left_ > 0) {
            void* next_ptr = static_cast<char*>(read_buffer_) + read_total_processed_;
            IoUringManager::getInstance().cache_call(this, ID_READ, io_uring_prep_recv, fd_, next_ptr, read_bytes_left_, 0);
        } else {
            IoUringManager::getInstance().finalize_current_task(false, read_total_processed_);
        }
    }

    void prepare_write(int res) {
        write_bytes_left_ -= res;
        write_total_processed_ += res;
        if (write_bytes_left_ > 0) {
            void* next_ptr = static_cast<char*>(write_buffer_) + write_total_processed_;
            IoUringManager::getInstance().cache_call(this, ID_WRITE, io_uring_prep_send, fd_, next_ptr, write_bytes_left_, MSG_NOSIGNAL);
        } else {
            IoUringManager::getInstance().finalize_current_task(false, write_total_processed_);
        }
    }

protected:
    void* read_buffer_;
    size_t read_bytes_left_;
    size_t read_total_processed_;

    void* write_buffer_;
    size_t write_bytes_left_;
    size_t write_total_processed_;
};
