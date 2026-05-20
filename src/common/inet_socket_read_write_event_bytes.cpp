#pragma once

#include <cstdint>
#include <utility>

#include "io_event.cpp"
#include "defs.cpp"

using namespace std;

class InetSocketReadWriteEventBytes : public Event {
public:
    InetSocketReadWriteEventBytes(vector<shared_ptr<File>> file)
        : Event(file) {}

    void construct_with_global() override {
    }

    CallResponse read(uint64_t, char* buf, size_t len, bool read_all = true) {
        sticky_read_ = read_all;
        read_buffer_ = buf;
        read_bytes_left_ = len;
        read_total_processed_ = 0;
        c(ID_READ, io_uring_prep_recv, files_[0]->get(), read_buffer_, read_bytes_left_, 0);
        return {"Read len bytes into buf", true, nullopt, OpHint::OP_HINT_READ | OpHint::OP_HINT_NETWORK};
    }

    CallResponse write(uint64_t, char* buf, size_t len) {
        write_buffer_ = buf;
        write_bytes_left_ = len;
        write_total_processed_ = 0;
        c(ID_WRITE, io_uring_prep_send, files_[0]->get(), write_buffer_, write_bytes_left_, MSG_NOSIGNAL);
        return {"Write len bytes from buf", true, nullopt, OpHint::OP_HINT_WRITE | OpHint::OP_HINT_NETWORK};
    }

    pair<GetDataInfo, void*> get_data(uint64_t id) {
        if (id == ID_READ) {
            return {{true}, read_buffer_};
        }
        return {{false}, nullptr};
    }

    optional<pair<bool, int>> on_yield(EventType event) override {
        auto res = get<IoUringResult>(event);
        if (res.res <= 0) {
            return pair{true, res.res};
        }
        
        if (res.op == ID_READ) {
            return prepare_read(res.res);
        } else if (res.op == ID_WRITE) {
            return prepare_write(res.res);
        }
        return nullopt;
    }

    string get_info() const override {
        return "inet socket readwriter FD " + to_string(files_[0]->get());
    }

private:
    bool sticky_read_ = false;

    optional<pair<bool, int>> prepare_read(int res) {
        read_bytes_left_ -= res;
        read_total_processed_ += res;
        if (sticky_read_ && read_bytes_left_ > 0) {
            void* next_ptr = static_cast<char*>(read_buffer_) + read_total_processed_;
            c(ID_READ, io_uring_prep_recv, files_[0]->get(), next_ptr, read_bytes_left_, 0);
            return nullopt;
        } else {
            return pair{false, read_total_processed_};
        }
    }

    optional<pair<bool, int>> prepare_write(int res) {
        write_bytes_left_ -= res;
        write_total_processed_ += res;
        if (write_bytes_left_ > 0) {
            void* next_ptr = static_cast<char*>(write_buffer_) + write_total_processed_;
            c(ID_WRITE, io_uring_prep_send, files_[0]->get(), next_ptr, write_bytes_left_, MSG_NOSIGNAL);
            return nullopt;
        } else {
            return pair{false, write_total_processed_};
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
