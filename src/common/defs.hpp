#pragma once

class IoEvent;

enum RequestID {
    ID_DEFAULT = 0,
    ID_READ = 1,
    ID_WRITE = 2
};

struct EventData {
    int id;
    IoEvent* event;
};
