#pragma once

#include <memory>
#include <utility>

#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>

#include "inet_socket_read_write_event_bytes.cpp"
#include "io_event.cpp"
#include "io_uring_manager.cpp"
#include "tls_socket.cpp"
#include "defs.cpp"

#include <boost/optional/optional_io.hpp>
using namespace std;
namespace http = boost::beast::http;

/**
 * @brief Base implementation for HTTP client reading using Boost.Beast parsers.
 */
class ReadWriteHTTPC : public Event {
public:
    ReadWriteHTTPC(vector<shared_ptr<File>> file, bool use_ssl = false);

    virtual ~ReadWriteHTTPC();

    /**
     * @brief Initiates or restarts the HTTP reading process.
     * Reassigns the parser for a new request and processes any leftover data.
     */
    CallResponse read_http(uint64_t);

    /**
     * @brief Returns the completed parser by moving it out.
     */
    pair<GetDataInfo, unique_ptr<http::response_parser<http::string_body>>> get_data(uint64_t);

    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    CallResponse write_http(uint64_t, http::request<http::string_body> res);

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    void on_new_data(int, EventType event) override;

    void procedure_update(PUType, CallResponse) override {

    }

    string get_info() const override;

protected:
    bool read_op_;
    uint64_t taskid_writer_, taskid_reader_;
    unique_ptr<http::response_parser<http::string_body>> parser_;
    boost::beast::flat_buffer buffer_;
    boost::beast::flat_buffer write_buffer_;

    bool tls_enabled_;

    void handle_read(int res);

    void handle_write(int res);

    void arm_read();

    void arm_write();

    bool try_parse();
};

