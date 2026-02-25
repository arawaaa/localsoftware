#pragma once

#include "io_event.hpp"
#include "io_uring_manager.hpp"
#include "defs.hpp"
#include <utility>

class InetSocketReadWriteEventBytes : public IoEvent {
public:
    InetSocketReadWriteEventBytes(std::unique_ptr<File> file, 
                             void* read_buf, size_t read_len,
                             void* write_buf, size_t write_len) 
        : IoEvent(std::move(file)), 
          read_buffer_(read_buf), read_bytes_left_(read_len), read_total_processed_(0),
          write_buffer_(write_buf), write_bytes_left_(write_len), write_total_processed_(0) {}

    InetSocketReadWriteEventBytes(int fd, 
                             void* read_buf, size_t read_len,
                             void* write_buf, size_t write_len) 
        : IoEvent(fd), 
          read_buffer_(read_buf), read_bytes_left_(read_len), read_total_processed_(0),
          write_buffer_(write_buf), write_bytes_left_(write_len), write_total_processed_(0) {}

    void read_all(void* buf, size_t len) {
        read_buffer_ = buf;
        read_bytes_left_ = len;
        read_total_processed_ = 0;
        IoUringManager::getInstance().cache_call(this, ID_READ, io_uring_prep_recv, fd_, read_buffer_, read_bytes_left_, 0);
    }

    void write_all(void* buf, size_t len) {
        write_buffer_ = buf;
        write_bytes_left_ = len;
        write_total_processed_ = 0;
        IoUringManager::getInstance().cache_call(this, ID_WRITE, io_uring_prep_send, fd_, write_buffer_, write_bytes_left_, MSG_NOSIGNAL);
    }

    std::pair<bool, int> abstract_event_success(int id, int res) override {
        if (res <= 0) {
            return {false, res};
        }
        
        if (id == ID_READ) {
            bool done = prepare_read(res);
            return {done, static_cast<int>(read_total_processed_)};
        } else if (id == ID_WRITE) {
            bool done = prepare_write(res);
            return {done, static_cast<int>(write_total_processed_)};
        }
        
        return {true, res};
    }

    std::pair<GetDataInfo, void*> get_data(int id) {
        if (id == ID_READ) {
            return {{true}, read_buffer_};
        }
        return {{false}, nullptr};
    }

private:
    bool prepare_read(int res) {
        read_bytes_left_ -= res;
        read_total_processed_ += res;
        if (read_bytes_left_ > 0) {
            void* next_ptr = static_cast<char*>(read_buffer_) + read_total_processed_;
            IoUringManager::getInstance().cache_call(this, ID_READ, io_uring_prep_recv, fd_, next_ptr, read_bytes_left_, 0);
            return false;
        }
        return true;
    }

    bool prepare_write(int res) {
        write_bytes_left_ -= res;
        write_total_processed_ += res;
        if (write_bytes_left_ > 0) {
            void* next_ptr = static_cast<char*>(write_buffer_) + write_total_processed_;
            IoUringManager::getInstance().cache_call(this, ID_WRITE, io_uring_prep_send, fd_, next_ptr, write_bytes_left_, MSG_NOSIGNAL);
            return false;
        }
        return true;
    }

protected:
    void* read_buffer_;
    size_t read_bytes_left_;
    size_t read_total_processed_;

    void* write_buffer_;
    size_t write_bytes_left_;
    size_t write_total_processed_;
};
