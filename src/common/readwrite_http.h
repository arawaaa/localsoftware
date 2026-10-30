#pragma once

#include <memory>
#include <utility>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/optional/optional_io.hpp>

#include "event.h"
#include "defs.h"

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
    CallResponse read_http(uint64_t);

    /**
     * @brief Returns the completed parser by moving it out.
     */
    unique_ptr<http::request_parser<http::string_body>> get_data(uint64_t);

    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    CallResponse write_http(uint64_t, http::response<http::string_body> res);

    CallResponse write_http_with_file_body(uint64_t, http::response_header<> header, std::string file, int dirfd);

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    optional<pair<bool, int>> on_yield(EventType event) override;

    string get_info() const override;

protected:
    bool read_op_;
    uint64_t taskid_writer_, taskid_reader_;
    unique_ptr<http::request_parser<http::string_body>> parser_;
    boost::beast::flat_buffer buffer_;
    boost::beast::flat_buffer write_buffer_;

    bool tls_enabled_;

    optional<pair<bool, int>> handle_read(int res);

    optional<pair<bool, int>> handle_write(int res);

    void arm_read();

    void arm_write();

    bool try_parse();
};

