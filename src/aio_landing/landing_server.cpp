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


AioLandingHTTP::AioLandingHTTP(vector<shared_ptr<File>> file, bool enable_tls, const HTTPManager* http_manager)
    : Event(file), http_manager_(http_manager)
{
    i<ReadWriteHTTP>(files_, enable_tls);
}

CallResponse AioLandingHTTP::start(uint64_t) {
    c(&ReadWriteHTTP::read_http);
    op_read_ = true;
    timer_ = timer(ts);
    return {"Server start up HTTP", true, nullopt, OP_HINT_NETWORK};
}

optional<pair<bool, int>> AioLandingHTTP::on_yield(EventType event) {
    return visit(overloaded {
        [this](ChildTaskCompletion& child) -> optional<pair<bool, int>> {
            if (child.return_code <= 0) {
                return pair{true, child.return_code};
            }

            if (op_read_) {
                cancel_timer(timer_);
                auto req = direct_access(0, &ReadWriteHTTP::get_data, child.task_id).value()->get();
                handle_request(req);
            } else {
                timer_ = timer(ts);
                handle_response();
            }
            return nullopt;
        },
        [](Timeout&) -> optional<pair<bool, int>> {
            return pair{true, -1};
        },
        [](auto&) -> optional<pair<bool, int>> {
            return nullopt;
        }
    }, event);
}

string AioLandingHTTP::get_info() const {
    return "AioLandingHTTP FD " + to_string(files_[0]->get());
}

void AioLandingHTTP::handle_request(boost::beast::http::request_parser<boost::beast::http::string_body>::value_type& req) {
    c(&ReadWriteHTTP::write_http, http_manager_->handle_request(req));
    op_read_ = false;
}

void AioLandingHTTP::handle_response() {
    c(&ReadWriteHTTP::read_http);
    op_read_ = true;
}
