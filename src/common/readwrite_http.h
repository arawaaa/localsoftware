#pragma once

#include <memory>
#include <utility>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/optional/optional_io.hpp>

#include "event.h"
#include "defs.h"
#include "coroutine/pinned.h"
#include "socket_adaptors/tls_socket.h"
#include "socket_adaptors/readwrite_bytes.h"

using namespace std;
namespace http = boost::beast::http;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class ReadWriteHTTP : public Event {
public:
    ReadWriteHTTP(vector<shared_ptr<File>> file, bool use_ssl = false);

    /**
     * @brief Initiates or restarts the HTTP reading process.
     * Reassigns the parser for a new request and processes any leftover data.
     */
    pinned<variant<int, unique_ptr<http::request_parser<http::string_body>>>> read_http();

    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    pinned<int> write_http(http::response<http::string_body> res);

protected:
    variant<ReadWriteBytes, TLSSocket> socket_;
    unique_ptr<http::request_parser<http::string_body>> parser_;
    // boost::beast::flat_buffer buffer_;
    // boost::beast::flat_buffer write_buffer_;

    bool tls_enabled_;

    bool try_parse(boost::beast::flat_buffer& buffer);

    optional<pair<bool, int>> handle_write(int res);
};

