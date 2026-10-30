#pragma once

#include <cstdint>
#include <utility>

#include "event.h"
#include "defs.h"

using namespace std;

class ReadWriteBytes : public Event {
public:
    ReadWriteBytes(vector<shared_ptr<File>> file);

    CallResponse read(uint64_t, char* buf, size_t len, bool read_all = true);

    CallResponse write(uint64_t, char* buf, size_t len);

    pair<GetDataInfo, void*> get_data(uint64_t id);

    optional<pair<bool, int>> on_yield(EventType event) override;

    string get_info() const override;

private:
    bool sticky_read_ = false;

    optional<pair<bool, int>> prepare_read(int res);

    optional<pair<bool, int>> prepare_write(int res);

protected:
    void* read_buffer_;
    size_t read_bytes_left_;
    size_t read_total_processed_;

    void* write_buffer_;
    size_t write_bytes_left_;
    size_t write_total_processed_;
};
