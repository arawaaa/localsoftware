#pragma once

#include <cstdint>
#include <utility>

#include "event.h"
#include "defs.h"
#include "coro_pinned.h"

using namespace std;

class ReadWriteBytes : public Event {
public:
    ReadWriteBytes(vector<shared_ptr<File>> file);

    pinned<int> read(void* buf, size_t len, bool read_all = true);

    pinned<int> write(void* buf, size_t len);
};
