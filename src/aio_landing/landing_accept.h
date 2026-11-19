#pragma once

#include <list>

#include "event.h"
#include "defs.h"
#include "http_manager.h"
#include "coroutine/pinned.h"
#include "landing_server.h"

using namespace std;

class LandingAccept : public Event {
public:
    LandingAccept(shared_ptr<File> server_file, bool use_tls, const string& base_dir);

    pinned<void> start();

private:
    struct sockaddr_in6 client_addr_{};
    socklen_t client_addr_len_ = sizeof(sockaddr_in6);
    bool use_tls_;
    HTTPManager http_manager_;
    list<optional<pair<AioLandingHTTP, optional<pinned<void>>>>> servers_;
};
