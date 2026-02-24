#pragma once

#include <map>
#include <typeindex>

class IoEvent;

enum RequestID {
    ID_DEFAULT = 0x1,
    ID_READ = 0x2,
    ID_WRITE = 0x4,
    FLAG_INTERNAL = 0x8,
    FLAG_REDO_CACHED_DATA = 0x10
};

struct EventData {
    int id;
    IoEvent* event;
};
