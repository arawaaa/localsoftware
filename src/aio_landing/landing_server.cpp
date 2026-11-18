#include "landing_server.h"

#include <boost/beast/http/string_body.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <variant>

#include "defs.h"
#include "readwrite_http.h"
// #include "http_manager.h"

using namespace std;

// TODO Reimplement timeouts with AnyOf

AioLandingHTTP::AioLandingHTTP(vector<shared_ptr<File>> file, bool enable_tls, const HTTPManager* http_manager)
    : Event(file), http_manager_(http_manager), http_adaptor_(file, enable_tls)
{
}

pinned<int> AioLandingHTTP::start() {
    typedef unique_ptr<http::request_parser<http::string_body>> ptparser;
    while (true) {
        auto request = co_await http_adaptor_.read_http();

        if (holds_alternative<int>(request))
            co_return get<int>(request);

        co_await http_adaptor_.write_http(http_manager_->handle_request(get<ptparser>(request)->get()));
    }
}
