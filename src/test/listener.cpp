#include <cstdint>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>
#include <optional>

#include "common/io_event.cpp"
#include "common/defs.cpp"

class Listener : public Event {
public:
    Listener() {

    }

    void construct_with_global() override {
        cout << "Constructed" << endl;
    }

    CallResponse init(uint64_t taskid) {
        std::cout << "Subprocess " << taskid << std::endl;
        return {"HTTP accepter", true, nullopt, OP_HINT_NETWORK};
    }

    optional<pair<bool, int>> on_yield(EventType) override {
        return pair {false, 0};
    }

    string get_info() const override { return "Test"; }
};
