#include "readwrite_bytes.h"

#include <cstdint>
#include <utility>

#include "event.h"
#include "defs.h"

using namespace std;

ReadWriteBytes::ReadWriteBytes(vector<shared_ptr<File>> file)
    : Event(file) {
    }

pinned<int> ReadWriteBytes::read(void* buf, size_t len, bool read_all) {
    int read_total_processed = 0;

    while (read_total_processed < len && (read_all || !read_total_processed)) {
        int res = co_await c(io_uring_prep_recv, files_[0]->get(), (char*)buf + read_total_processed, len - read_total_processed, 0);

        if (res <= 0)
            co_return res;

        read_total_processed += res;
    }

    co_return read_total_processed;
}

pinned<int> ReadWriteBytes::write(void* buf, size_t len) {
    int write_total_processed = 0;

    while (write_total_processed < len) {
        int res = co_await c(io_uring_prep_send, files_[0]->get(), (char*)buf + write_total_processed, len - write_total_processed, MSG_NOSIGNAL);

        if (res <= 0)
            co_return res;

        write_total_processed += res;
    }

    co_return write_total_processed;
}
