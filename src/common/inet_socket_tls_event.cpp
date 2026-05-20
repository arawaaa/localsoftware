#pragma once

#include <memory>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "defs.cpp"
#include "inet_socket_read_write_event_bytes.cpp"
#include "io_event.cpp"
#include "io_uring_manager.cpp"

#define MAXFRAMELENGTH (size_t)16384
#define MAXUFRAMELENGTH (size_t)(16384 - 1024) // little bit of margin

using namespace std;
/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class InetSocketTLSEvent : public Event {
public:
    InetSocketTLSEvent(vector<shared_ptr<File>> file, bool server)
        : Event(file), server_(server)
    {
        if ((ssl_ = SSL_new(AsyncHandler::self().get_tls_ctx())) == NULL) {
            throw runtime_error{"Failed to create ssl object"};
        }

        readbuf_ = BIO_new(BIO_s_mem());
        writebuf_ = BIO_new(BIO_s_mem());
        SSL_set_bio(ssl_, readbuf_, writebuf_);
        state_ = TLSState::WaitHello;
    }

    void construct_with_global() override {
        i<InetSocketReadWriteEventBytes>(files_);
    }

    virtual ~InetSocketTLSEvent() {
        if (ssl_) {
            SSL_free(ssl_);
        }
    }

    CallResponse read(uint64_t id, char* buf, size_t len, bool read_all = true) {
        sticky_read_ = read_all;
        task_read_ = id;
        u_read_ = buf;
        u_readlen_ = len;
        u_read_p_ = 0;
        handle_read();
        return {"Read len bytes into buf TLS", true, nullopt, OpHint::OP_HINT_READ | OpHint::OP_HINT_NETWORK};
    }

    CallResponse write(uint64_t id, char* buf, size_t len) {
        task_write_ = id;
        u_write_ = buf;
        u_writelen_ = len;
        u_write_p_ = 0;
        handle_write();
        return {"Write len bytes from buf TLS", true, nullopt, OpHint::OP_HINT_WRITE | OpHint::OP_HINT_NETWORK};
    }

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    optional<pair<bool, int>> on_yield(EventType event) override {
        auto res = get<ChildTaskCompletion>(event);
        if (res.return_code <= 0) {
            return pair{true, res.return_code};
        }

        if (res.task_id == task_bytes_read_ && !task_bytes_r_fin_) {
            task_bytes_r_fin_ = true;
            BIO_write(readbuf_, e_read_, res.return_code);
        } else if (res.task_id == task_bytes_write_) {
            task_bytes_w_fin_ = true;
        }

        if (res.calling_id == task_read_) {
            return handle_read();
        } else if (res.calling_id == task_write_) {
            return handle_write();
        } else {
            throw runtime_error{"Unknown task type"};
        }
    }

    string get_info() const override {
        return "TLS socket adaptor";
    }

protected:
    uint64_t task_bytes_read_, task_bytes_write_;
    bool task_bytes_r_fin_ = true, task_bytes_w_fin_ = true;
    uint64_t task_read_, task_write_;
    bool server_;

    bool sticky_read_ = false;
    char* u_read_, *u_write_;
    size_t u_readlen_, u_writelen_, u_read_p_, u_write_p_;

    // TLS variables. Buffer procession for recv: recv -> encryptread -> ssl_read -> buffer_ -> parse
    enum TLSState {
        WaitHello,
        Full,
        Failed
    };

    SSL* ssl_;
    BIO *writebuf_, *readbuf_;
    // The write function will take care of the entire buffer
    size_t e_writelen_;
    char e_read_[MAXFRAMELENGTH], e_write_[MAXFRAMELENGTH];
    TLSState state_;

    optional<pair<bool, int>> handle_handshake() {
        int ec = server_ ? SSL_accept(ssl_) : SSL_connect(ssl_);
        if (SSL_get_error(ssl_, ec) == SSL_ERROR_WANT_READ) {
            if (BIO_ctrl_pending(writebuf_)) {
                arm_write();
            } else {
                arm_read();
            }
        } else if (SSL_get_error(ssl_, ec) != SSL_ERROR_NONE) {
            ERR_print_errors_fp(stderr);
            state_ = TLSState::Failed;
            return pair{true, -1};
        } else {
            state_ = TLSState::Full;
        }
        return nullopt;
    }

    optional<pair<bool, int>> handle_read() {
        if (state_ == TLSState::WaitHello) {
            auto res = handle_handshake();
            if (res) return res;
        }

        if (state_ == TLSState::Full) { do {
            size_t readbytes = 0;
            int ret = SSL_read_ex(ssl_, u_read_ + u_read_p_, u_readlen_ - u_read_p_, &readbytes);
            u_read_p_ += readbytes;

            // If bytes are read, it will not have an error
            switch (SSL_get_error(ssl_, ret)) {
                case SSL_ERROR_WANT_READ:
                    if (BIO_ctrl_pending(writebuf_)) {
                        arm_write();
                    } else {
                        arm_read();
                    }
                    return nullopt;
                case SSL_ERROR_NONE:
                    if (u_readlen_ - u_read_p_ == 0 || (!sticky_read_ && u_read_p_)) {
                        // Done, or received initial message only
                        return pair{false, u_read_p_};
                    } else if (!BIO_ctrl_pending(readbuf_)) {
                        // Nothing else in the BIO, still incomplete. Continue
                        arm_read();
                    } // else { repeat }
                    break;
                default:
                    // What to do in this case? I think ssl_write will also error out
                    ERR_print_errors_fp(stderr);
                    return pair{true, -1};
            }
        } while (BIO_ctrl_pending(readbuf_) && (u_readlen_ - u_read_p_) && (sticky_read_ || !u_read_p_));}

        return nullopt;
    }

    optional<pair<bool, int>> handle_write() {
        if (state_ == TLSState::WaitHello) {
            auto res = handle_handshake();
            if (res) return res;
        }

        if (state_ == TLSState::Full) {
            size_t numread = 0;
            int ret = SSL_write_ex(ssl_, u_write_ + u_write_p_, min(u_writelen_ - u_write_p_, MAXUFRAMELENGTH), &numread);
            u_write_p_ += numread;

            switch (SSL_get_error(ssl_, ret)) {
                case SSL_ERROR_WANT_READ:
                    if (BIO_ctrl_pending(writebuf_)) {
                        arm_write();
                    } else {
                        arm_read();
                    }
                    break;
                case SSL_ERROR_NONE:
                    if (BIO_ctrl_pending(writebuf_) || (u_writelen_ - u_write_p_)) {
                        arm_write();
                    } else {
                        return pair{false, u_writelen_};
                    }
                    break;
                default:
                    ERR_print_errors_fp(stderr);
                    return pair{true, -1};
            }
        }
        return nullopt;
    }

    void arm_read() {
        if (task_bytes_r_fin_) {
            uint64_t taskid = c(&InetSocketReadWriteEventBytes::read, e_read_, sizeof(e_read_), false);
            task_bytes_read_ = taskid;
            task_bytes_r_fin_ = false;
        } else {
            // Async attach has to be a bit different...
            AsyncHandler::self().attach_child(task_bytes_read_);
        }
    }

    void arm_write() {
        if (task_bytes_w_fin_) {
            BIO_read_ex(writebuf_, e_write_, sizeof(e_write_), &e_writelen_);
            uint64_t taskid = c(&InetSocketReadWriteEventBytes::write, e_write_, e_writelen_);
            task_bytes_write_ = taskid;
            task_bytes_w_fin_ = false;
        } else {
            AsyncHandler::self().attach_child(task_bytes_write_);
        }
    }
};
