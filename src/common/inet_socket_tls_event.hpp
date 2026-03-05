#pragma once

#include <memory>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "defs.hpp"
#include "common/inet_socket_read_write_event_bytes.hpp"
#include "io_event.hpp"
#include "io_uring_manager.hpp"

#define MAXFRAMELENGTH (size_t)16384
#define MAXUFRAMELENGTH (size_t)(16384 - 1024) // little bit of margin

using namespace std;
/**
 * @brief Base implementation for HTTP socket reading using Boost.Beast parsers.
 */
class InetSocketTLSEvent : public IoEvent {
public:
    InetSocketTLSEvent(vector<shared_ptr<File>> file)
        : IoEvent(file)
    {
        if ((ssl_ = SSL_new(IoUringManager::getInstance().get_tls_ctx())) == NULL) {
            throw runtime_error{"Failed to create ssl object"};
        }

        IoUringManager::getInstance().initialize_dependent_event<InetSocketReadWriteEventBytes>(this, file);

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

    CallResponse read(uint64_t, char* buf, size_t len, bool read_all = true) {
        sticky_read_ = read_all;
        op_read_ = true;
        u_read_ = buf;
        u_readlen_ = len;
        u_read_p_ = 0;
        handle_read(0);
        return {"Read len bytes into buf TLS", true, OpHint::OP_HINT_READ | OpHint::OP_HINT_NETWORK};
    }

    CallResponse write(uint64_t, char* buf, size_t len) {
        op_read_ = false;
        u_write_ = buf;
        u_writelen_ = len;
        u_write_p_ = 0;
        handle_write(0);
        return {"Write len bytes from buf TLS", true, OpHint::OP_HINT_WRITE | OpHint::OP_HINT_NETWORK};
    }

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    void on_new_data(int, EventType event) override {
        auto res = get<ChildTaskCompletion>(event);
        if (res.return_code <= 0) {
            IoUringManager::getInstance().finalize_current_task(true, res.return_code);
            IoUringManager::getInstance().consume_event(res.task_id);
            return;
        }

        if (op_read_) {
            handle_read(res.task_id == taskid_read_ ? res.return_code : 0);
        } else {
            handle_write(res.task_id == taskid_write_ ? res.return_code : 0);
        }
        IoUringManager::getInstance().consume_event(res.task_id);
    }

    string get_info() const override {
        return "TLS socket adaptor";
    }

protected:
    uint64_t taskid_read_, taskid_write_;

    bool sticky_read_ = false, op_read_ = false;
    char* u_read_, *u_write_;
    size_t u_readlen_, u_writelen_, u_read_p_, u_write_p_;

    // TLS variables. Buffer procession for recv: recv -> encryptread -> ssl_read -> buffer_ -> parse
    enum TLSState {
        WaitHello,
        Full
    };

    SSL* ssl_;
    BIO *writebuf_, *readbuf_;
    // The write function will take care of the entire buffer
    size_t e_writelen_;
    char e_read_[MAXFRAMELENGTH], e_write_[MAXFRAMELENGTH];
    TLSState state_;

    void handle_read(int res) {
        BIO_write(readbuf_, e_read_, res);
        if (state_ == TLSState::WaitHello) {
            int ec = SSL_accept(ssl_);
            if (SSL_get_error(ssl_, ec) == SSL_ERROR_WANT_READ) {
                BIO_read_ex(writebuf_, e_write_, sizeof(e_write_), &e_writelen_);
                if (e_writelen_) {
                    arm_write();
                } else {
                    arm_read();
                }
            } else if (SSL_get_error(ssl_, ec) != SSL_ERROR_NONE) {
                ERR_print_errors_fp(stderr);
                IoUringManager::getInstance().finalize_current_task(true, -1);
            } else {
                state_ = TLSState::Full;
            }
        }
        if (state_ == TLSState::Full) { do {
            size_t readbytes = 0;
            int ret = SSL_read_ex(ssl_, u_read_ + u_read_p_, u_readlen_ - u_read_p_, &readbytes);
            u_read_p_ += readbytes;

            // If bytes are read, it will not have an error
            switch (SSL_get_error(ssl_, ret)) {
                case SSL_ERROR_WANT_READ:
                    if (BIO_ctrl_pending(writebuf_)) {
                        BIO_read_ex(writebuf_, e_write_, sizeof(e_write_), &e_writelen_);
                        arm_write();
                    } else {
                        arm_read();
                    }
                    return;
                case SSL_ERROR_NONE:
                    if (u_readlen_ - u_read_p_ == 0 || (!sticky_read_ && u_read_p_)) {
                        // Done, or received initial message only
                        IoUringManager::getInstance().finalize_current_task(false, u_read_p_);
                    } else if (!BIO_ctrl_pending(readbuf_)) {
                        // Nothing else in the BIO, still incomplete. Continue
                        arm_read();
                    }
                    break;
                default:
                    IoUringManager::getInstance().finalize_current_task(true, -1);
                    return;
            }
        } while (BIO_ctrl_pending(readbuf_) && (u_readlen_ - u_read_p_) && (sticky_read_ || !u_read_p_));}
    }

    void handle_write(int res) {
        if (res < 0) return IoUringManager::getInstance().finalize_current_task(true, res);

        size_t numread = 0;
        int ret = SSL_write_ex(ssl_, u_write_ + u_write_p_, min(u_writelen_ - u_write_p_, MAXUFRAMELENGTH), &numread);
        u_write_p_ += numread;

        switch (SSL_get_error(ssl_, ret)) {
            case SSL_ERROR_WANT_READ:
                if (BIO_ctrl_pending(writebuf_)) {
                    BIO_read_ex(writebuf_, e_write_, sizeof(e_write_), &e_writelen_);
                    arm_write();
                } else {
                    arm_read();
                }
                break;
            case SSL_ERROR_NONE:
                if (BIO_ctrl_pending(writebuf_) || (u_writelen_ - u_write_p_)) {
                    BIO_read_ex(writebuf_, e_write_, sizeof(e_write_), &e_writelen_);
                    arm_write();
                } else {
                    IoUringManager::getInstance().finalize_current_task(false, u_writelen_);
                }
                break;
            default:
                IoUringManager::getInstance().finalize_current_task(true, -1);
        }
    }

    void arm_read() {
        auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventBytes>(
            this,
            0,
            &InetSocketReadWriteEventBytes::read,
            e_read_,
            sizeof(e_read_),
            false // Must always be non-sticky since we don't know how much encrypted bytes to read for n unenc bytes
        );
        taskid_read_ = taskid;
    }

    void arm_write() {
        auto [taskid, success] = IoUringManager::getInstance().call_dependent_function<InetSocketReadWriteEventBytes>(
            this,
            0,
            &InetSocketReadWriteEventBytes::write,
            e_write_,
            e_writelen_
        );
        taskid_write_ = taskid;
    }
};
