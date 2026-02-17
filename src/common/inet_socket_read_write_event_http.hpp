#pragma once

#include "io_event.hpp"
#include "io_uring_manager.hpp"
#include "defs.hpp"
#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/buffer.hpp>
#include <memory>
#include <openssl/bio.h>
#include <utility>
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace http = boost::beast::http;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class InetSocketReadWriteEventHTTP : public IoEvent {
public:
    InetSocketReadWriteEventHTTP(std::unique_ptr<File> file, bool use_ssl = false)
        : IoEvent(std::move(file)), tls_enabled_(use_ssl)
    {
        if (tls_enabled_) {
            if ((ssl_ = SSL_new(IoUringManager::getInstance().get_tls_ctx())) == NULL) {
                throw std::runtime_error{"Failed to create ssl object"};
            }

            readbuf_ = BIO_new(BIO_s_mem());
            writebuf_ = BIO_new(BIO_s_mem());
            SSL_set_bio(ssl_, readbuf_, writebuf_);
            state_ = TLSState::WaitHello;
        }
    }

    virtual ~InetSocketReadWriteEventHTTP() {
        if (tls_enabled_) {
            if (ssl_) {
                SSL_free(ssl_);
            }
        }
    }

    /**
     * @brief Initiates or restarts the HTTP reading process.
     * Reassigns the parser for a new request and processes any leftover data.
     */
    void read_http() {
        parser_ = std::make_unique<http::request_parser<http::string_body>>();
        if (buffer_.size() > 0) {
            if (try_parse()) {
                // Already have a full request from leftover data.
                // Trigger a NOP to entry the event loop cycle and call post().
                IoUringManager::getInstance().cache_call(this, ID_READ, io_uring_prep_nop);
                return;
            }
        }
        arm_read(RequestID::ID_READ);
    }

    /**
     * @brief Returns the completed parser by moving it out.
     */
    std::unique_ptr<http::request_parser<http::string_body>> get_parser() {
        return std::move(parser_);
    }

    /**
     * @brief Serializes an HTTP response into the write buffer and starts the write process.
     */
    void write_http(http::response<http::string_body> res) {
        http::response_serializer<http::string_body> sr{res};
        boost::system::error_code ec;
        while (!sr.is_done()) {
            sr.next(ec, [&](auto const&, auto const& src) {
                // Copy from src (ConstBufferSequence) to write_buffer_
                size_t n = boost::asio::buffer_copy(write_buffer_.prepare(boost::asio::buffer_size(src)), src);
                write_buffer_.commit(n);
                sr.consume(n);
            });
            if (ec) break;
        }

        if (tls_enabled_) {
            // Start a non-uring event so that the writes can be handled in one logic flow
            IoUringManager::getInstance().add(RequestID::ID_WRITE, 0, this);
        } else {
            arm_write(RequestID::ID_WRITE);
        }
    }

    std::pair<bool, int> abstract_event_success(int id, int res) override {
        // The Redo cached data flag lets us create events with res = 0
        if (res <= 0 && !((id & RequestID::FLAG_REDO_CACHED_DATA) && res == 0)) return {true, -1}; // Error
        
        if (id & ID_READ) {
            if (tls_enabled_) {
                return handle_read_tls(id, res);
            } else {
                return handle_read(id, res);
            }
        } else if (id & ID_WRITE) {
            if (tls_enabled_) {
                return handle_write_tls(id, res);
            } else {
                return handle_write(id, res);
            }
        }
        // Invalid request
        return {true, -1};
    }

protected:
    bool read_retry_ = false, write_retry_ = false;
    bool read_active_ = false, write_active_ = false;
    std::unique_ptr<http::request_parser<http::string_body>> parser_;
    boost::beast::flat_buffer buffer_;
    boost::beast::flat_buffer write_buffer_;

    // TLS variables. Buffer procession for recv: recv -> encryptread -> ssl_read -> buffer_ -> parse
    enum TLSState {
        WaitHello,
        Full
    };

    SSL* ssl_;
    BIO *writebuf_, *readbuf_;
    char encryptread_[2048], encryptwrite_[2048];
    size_t write_length_;
    bool tls_enabled_;
    TLSState state_;
    std::function<int(size_t*)> read_func_cached_, write_func_cached_;

    std::pair<bool, int> handle_read_tls(int id, int res) {
        BIO_write(readbuf_, encryptread_, res);
        if (state_ == TLSState::WaitHello) {
            int ec = SSL_accept(ssl_);
            if (SSL_get_error(ssl_, ec) == SSL_ERROR_WANT_READ) {
                BIO_read_ex(writebuf_, encryptwrite_, 2048, &write_length_);
                if (write_length_) {
                    arm_write(id, true);
                } else {
                    arm_read(id);
                }
                return {false, res};
            } else if (SSL_get_error(ssl_, ec) != SSL_ERROR_NONE) { // SSL_ERROR_WANT_WRITE shouldn't happen
                ERR_print_errors_fp(stderr);
                return {true, -1};
            } else {
                state_ = TLSState::Full;
            }
        }
        if (state_ == TLSState::Full) { // TLSState::Full
            bool read_any = false;
            while (BIO_ctrl_pending(readbuf_)) {
                size_t readbytes = 0;
                int ret;
                if (read_retry_) {
                    ret = read_func_cached_(&readbytes);
                    read_retry_ = false;
                } else {
                    auto mutable_buffer = buffer_.prepare(2048);
                    read_func_cached_ = [&](size_t* readbytes) {
                        return SSL_read_ex(ssl_, mutable_buffer.data(), mutable_buffer.size(), readbytes);
                    };
                    ret = read_func_cached_(&readbytes);
                }
                switch (SSL_get_error(ssl_, ret)) {
                    case SSL_ERROR_WANT_READ:
                        read_retry_ = true;
                        if (BIO_ctrl_pending(writebuf_)) {
                            BIO_read_ex(writebuf_, encryptwrite_, 2048, &write_length_);
                            read_active_ = false;
                            arm_write(id, true);
                        } else {
                            arm_read(id, id & RequestID::FLAG_INTERNAL);
                        }
                        goto ret_retry; // shouldn't have actually written anything to buffer_
                    case SSL_ERROR_WANT_WRITE:
                        if (!read_any && id & RequestID::FLAG_INTERNAL) {
                            // A write op returned with SSL_ERROR_WANT_READ, and now we have SSL_ERROR_WANT_WRITE. Throw
                            throw std::runtime_error{"Libssl library error. Received WANT_WRITE & WANT_READ, unable to advance."};
                        }
                        read_retry_ = true;
                        read_active_ = false;
                        if (!(id & RequestID::FLAG_INTERNAL)) {
                            arm_write(id, true);
                            goto ret_retry;
                        } else {
                            // Non-kernel request to determine if we have read enough. If we haven't, this will restart the read
                            IoUringManager::getInstance().add(RequestID::ID_WRITE, 0, this);
                            goto ret_retry;
                        }
                    case SSL_ERROR_NONE:
                        read_any = true;
                        break;
                    default:
                        return {true, -1};
                }
                buffer_.commit(readbytes);
            }

            // Don't parse requests on internal-only requests, and only parse when the consumer asks for it
            if (!(id & RequestID::FLAG_INTERNAL) && try_parse()) {
                return {true, 1};
            } else if (!(id & RequestID::FLAG_INTERNAL))
                arm_read(id);
            else {
                read_active_ = false;
                IoUringManager::getInstance().add(RequestID::ID_WRITE, 0, this);
            }
        }

ret_retry:
        return {false, res};
    }

    std::pair<bool, int> handle_read(int id, int res) {
        if (res > 0) {
            buffer_.commit(res);
        }

        bool done = try_parse();
        if (done) {
            return {true, res};
        }

        arm_read(id);
        return {false, res};
    }

    std::pair<bool, int> handle_write_tls(int id, int res) {
        if (res < 0) return {true, -1};

        int max_write_length = 2048, offset = 0;
        if (res < write_length_) {
            memmove(encryptwrite_, encryptwrite_ + res, write_length_ - res);
            max_write_length = 2048 - (write_length_ - res);
        }
        write_length_ = offset = write_length_ - res;

        if (state_ == TLSState::WaitHello) {
            write_active_ = false;
            IoUringManager::getInstance().add(RequestID::ID_READ, 0, this);
            return {false, res};
        }

        size_t numread;
        int ret = SSL_write_ex(ssl_, write_buffer_.data().data(), write_buffer_.size(), &numread);

        switch (SSL_get_error(ssl_, ret)) {
            case SSL_ERROR_WANT_READ:
                if (id & RequestID::FLAG_INTERNAL)
                    throw std::runtime_error{"WANT_WRITE followed by WANT_READ"};
                // Make sure our entire write BIO is sent out to make sure the read "works"
                write_retry_ = true;
                if (BIO_ctrl_pending(writebuf_) || write_length_) {
                    BIO_read(writebuf_, encryptwrite_ + offset, 2048 - write_length_);
                    arm_write(id);
                } else {
                    // Nothing else to write out, start read
                    write_active_ = false;
                    arm_read(id, true);
                }
                return {false, res};
            case SSL_ERROR_WANT_WRITE:
                write_retry_ = true;
                arm_write(id, id & RequestID::FLAG_INTERNAL);
                return {false, res};
            case SSL_ERROR_NONE:
                write_retry_ = false;
                write_buffer_.consume(numread);
                if (id & RequestID::FLAG_INTERNAL) {
                    write_active_ = false;
                    IoUringManager::getInstance().add(RequestID::ID_READ, 0, this);
                    return {false, res};
                }

                if (BIO_ctrl_pending(writebuf_) || write_buffer_.size()) {
                    int ret = BIO_read(writebuf_, encryptwrite_ + offset, 2048 - write_length_);
                    write_length_ += ret;
                    arm_write(id);
                    return {false, res};
                } else {
                    return {true, 1};
                }
            default:
                return {true, -1};
        }
    }

    std::pair<bool, int> handle_write(int id, int res) {
        if (res > 0) {
            write_buffer_.consume(res);
        }

        if (write_buffer_.size() > 0) {
            arm_write(id);
            return {false, res};
        }
        return {true, res};
    }

    void arm_read(int id, bool internal = false) {
        if (read_active_ && !(id & RequestID::ID_READ)) return;
        read_active_ = true;
        if (tls_enabled_) {
            IoUringManager::getInstance().cache_call(this, ID_READ | (internal ? FLAG_INTERNAL : 0), io_uring_prep_recv, fd_,
                                                    encryptread_, 2048, 0);
        } else {
            // Prepare space in the flat_buffer for the next read
            auto mutable_buffer = buffer_.prepare(2048);
            IoUringManager::getInstance().cache_call(this, ID_READ | (internal ? FLAG_INTERNAL : 0), io_uring_prep_read, fd_,
                                                    mutable_buffer.data(), mutable_buffer.size(), 0);
        }
    }

    void arm_write(int id, bool internal = false) {
        if (write_active_ && !(id & RequestID::ID_WRITE)) return;
        write_active_ = true;

        if (tls_enabled_) {
            IoUringManager::getInstance().cache_call(this, ID_WRITE | (internal ? FLAG_INTERNAL : 0), io_uring_prep_send, fd_, encryptwrite_,
                                                     write_length_, MSG_NOSIGNAL);
        } else {
            if (write_buffer_.size() > 0) {
                IoUringManager::getInstance().cache_call(this, ID_WRITE | (internal ? FLAG_INTERNAL : 0), io_uring_prep_send, fd_,
                                                        write_buffer_.data().data(), write_buffer_.size(), MSG_NOSIGNAL);
            }
        }
    }

    bool try_parse() {
        boost::system::error_code ec;
        size_t consumed = parser_->put(buffer_.data(), ec);
        buffer_.consume(consumed);

        if (ec && ec != http::error::need_more) {
            // Treat parse errors as "done" so the caller can handle the error state in the parser
            return true;
        }

        return parser_->is_done();
    }
};

/**
 * @brief Identical to InetSocketReadWriteEventHTTP, provided for naming consistency.
 */
class InetSocketReadWriteEventBytesHTTP : public InetSocketReadWriteEventHTTP {
public:
    using InetSocketReadWriteEventHTTP::InetSocketReadWriteEventHTTP;
};
