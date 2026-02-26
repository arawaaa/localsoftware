#pragma once

#include "common/inet_socket_read_write_event_bytes.hpp"
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

#define MAXFRAMELENGTH (size_t)16384
#define MAXUFRAMELENGTH (size_t)(16384 - 1024) // little bit of margin
namespace http = boost::beast::http;

/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class InetSocketTLSEvent : public IoEvent {
public:
    InetSocketTLSEvent(std::unique_ptr<File> file, bool use_ssl = false)
        : IoEvent(std::move(file))
    {
        if ((ssl_ = SSL_new(IoUringManager::getInstance().get_tls_ctx())) == NULL) {
            throw std::runtime_error{"Failed to create ssl object"};
        }

        IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventBytes>(this, std::move(file));

        readbuf_ = BIO_new(BIO_s_mem());
        writebuf_ = BIO_new(BIO_s_mem());
        SSL_set_bio(ssl_, readbuf_, writebuf_);
        state_ = TLSState::WaitHello;
    }

    virtual ~InetSocketTLSEvent() {
        if (ssl_) {
            SSL_free(ssl_);
        }
    }

    void read(char* buf, size_t len) {
        u_read_ = buf;
        u_readlen_ = len;
        u_read_p_ = 0;
        handle_read(ID_READ, 0);
    }

    void write(char* buf, size_t len) {
        u_write_ = buf;
        u_writelen_ = len;
        u_write_p_ = 0;
        handle_write(ID_WRITE, 0);
    }

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    void on_new_data(int op, EventType event) override {
        int res = std::get<IoUringResult>(event).res;

        if (op & ID_READ) {
            handle_read(op, res);
        } else if (op & ID_WRITE) {
            handle_write(op, res);
        }
    }

protected:
    bool read_retry_ = false, write_retry_ = false;
    bool read_active_ = false, write_active_ = false;
    char* u_read_, *u_write_;
    size_t u_readlen_, u_writelen_, u_read_p_, u_write_p_;

    // TLS variables. Buffer procession for recv: recv -> encryptread -> ssl_read -> buffer_ -> parse
    enum TLSState {
        WaitHello,
        Full
    };

    SSL* ssl_;
    BIO *writebuf_, *readbuf_;
    size_t e_readlen_, e_writelen_;
    char e_read_[4096], e_write_[4096];
    TLSState state_;
    std::function<int(size_t*)> read_func_cached_, write_func_cached_;

    bool handle_read(int id, int res) {
        BIO_write(readbuf_, e_read_, res);
        if (state_ == TLSState::WaitHello) {
            int ec = SSL_accept(ssl_);
            if (SSL_get_error(ssl_, ec) == SSL_ERROR_WANT_READ) {
                BIO_read_ex(writebuf_, e_write_, sizeof(e_write_), &e_writelen_);
                if (e_writelen_) {
                    arm_write(id);
                } else {
                    arm_read(id);
                }
                return false;
            } else if (SSL_get_error(ssl_, ec) != SSL_ERROR_NONE) {
                ERR_print_errors_fp(stderr);
                return true;
            } else {
                state_ = TLSState::Full;
            }
        }
        if (state_ == TLSState::Full) {
            bool read_any = false;
            while (BIO_ctrl_pending(readbuf_)) {
                size_t readbytes = 0;
                int ret;

                SSL_read_ex(ssl_, u_read_ + u_read_p_, u_readlen_ - u_read_p_, &readbytes);

                u_read_p_ += readbytes;

                switch (SSL_get_error(ssl_, ret)) {
                    case SSL_ERROR_WANT_READ:
                        if (BIO_ctrl_pending(writebuf_)) {
                            BIO_read_ex(writebuf_, e_write_, sizeof(e_write_), &e_writelen_);
                            arm_write(id);
                        } else {
                            arm_read(id);
                        }
                        return false;
                    case SSL_ERROR_WANT_WRITE:
                        arm_write(id);
                        return false;
                    case SSL_ERROR_NONE:
                        read_any = true;
                        break;
                    default:
                        return true;
                }
            }

            if (try_parse()) {
                return true;
            } else {
                arm_read(id);
                return false;
            }
        }

        return false;
    }

    bool handle_write(int id, int res) {
        if (res < 0) return true;

        int offset = 0;
        u_write_p_ += res;

        size_t numread;
        int ret = SSL_write_ex(ssl_, u_write_ + u_read_p_, std::min(u_readlen_ - u_read_p_, MAXUFRAMELENGTH), &numread);

        switch (SSL_get_error(ssl_, ret)) {
            case SSL_ERROR_WANT_READ:
                write_retry_ = true;
                if (BIO_ctrl_pending(writebuf_) || write_length_) {
                    BIO_read(writebuf_, e_write_ + offset, 4096 - write_length_);
                    arm_write(id);
                } else {
                    write_active_ = false;
                    arm_read(id);
                }
                return false;
            case SSL_ERROR_WANT_WRITE:
                write_retry_ = true;
                arm_write(id);
                return false;
            case SSL_ERROR_NONE:
                write_retry_ = false;
                write_buffer_.consume(numread);

                if (BIO_ctrl_pending(writebuf_) || write_buffer_.size()) {
                    int ret = BIO_read(writebuf_, e_write_ + offset, 4096 - write_length_);
                    write_length_ += ret;
                    arm_write(id);
                    return false;
                } else {
                    return true;
                }
            default:
                return true;
        }
    }

    void arm_read(int id) {
        IoUringManager::getInstance().cache_call(this, ID_READ, io_uring_prep_read, fd_,
                                                mutable_buffer.data(), mutable_buffer.size(), 0);
    }

    void arm_write(int id) {
        IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventBytes>(
            this,
            InetSocketReadWriteEventBytes::read, th);
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
