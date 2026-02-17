#pragma once

class IoEvent;

enum RequestID {
    ID_DEFAULT = 0x1,
    ID_READ = 0x2,
    ID_WRITE = 0x4,
    FLAG_INTERNAL=0x8
};

struct EventData {
    int id;
    IoEvent* event;
};
