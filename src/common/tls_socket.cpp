#include "tls_socket.h"

#include <memory>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "defs.h"
#include "readwrite_bytes.h"
#include "event.h"
#include "async_handler.h"

#define MAXFRAMELENGTH (size_t)(16 * 1024)
#define MAXUFRAMELENGTH (size_t)(16384 - 1024) // little bit of margin

using namespace std;

extern thread_local AllocatorWrapper alloc;

// Ossl callbacks

int osslcb_read(BIO* b, char* dat, size_t len, size_t *written);
long osslcb_ctrl(BIO *, int cmd, long, void *);

SSLCtx::SSLCtx() {
    if ((ssl_ = SSL_new(AsyncHandler::self().get_tls_ctx())) == nullptr) {
        throw runtime_error{"Failed to create ssl object"};
    }
}

SSLCtx::SSLCtx(SSLCtx&& other) {
    ssl_ = other.ssl_;
    other.ssl_ = nullptr;
}

SSLCtx& SSLCtx::operator=(SSLCtx&& other) {
    ssl_ = other.ssl_;
    other.ssl_ = nullptr;
    return *this;
}

SSL* SSLCtx::operator()() {
    return ssl_;
}

SSLCtx::~SSLCtx() {
    SSL_free(ssl_);
}

TLSSocket::TLSSocket(vector<shared_ptr<File>> file, bool server)
    : Event(file), server_(server)
{
    i<ReadWriteBytes>(files_);
    openssl_setup();

    e_read_ = static_cast<char*>(alloc.bmalloc(1));
    e_write_ = static_cast<char*>(alloc.bmalloc(1));
}

TLSSocket::~TLSSocket() {
    alloc.bfree(e_read_);
    alloc.bfree(e_write_);
}

void TLSSocket::openssl_setup() {
    BIO_METHOD* read_method = BIO_meth_new(BIO_TYPE_NONE, "ReadAsync");
    BIO_meth_set_read_ex(read_method, &osslcb_read);
    BIO_meth_set_ctrl(read_method, &osslcb_ctrl);
    readbuf_ = BIO_new(read_method);
    BIO_set_data(readbuf_, this);
    writebuf_ = BIO_new(BIO_s_mem());
    SSL_set_bio(ssl_(), readbuf_, writebuf_);
    state_ = TLSState::WaitHello;
}

CallResponse TLSSocket::read(uint64_t id, char* buf, size_t len, bool read_all) {
    sticky_read_ = read_all;
    task_read_ = id;
    u_read_ = buf;
    u_readlen_ = len;
    u_read_p_ = 0;
    handle_read();
    return {"Read len bytes into buf TLS", true, nullopt, OpHint::OP_HINT_READ | OpHint::OP_HINT_NETWORK};
}

CallResponse TLSSocket::write(uint64_t id, char* buf, size_t len) {
    if (u_write_p_ < u_writelen_) {
        // There is a write already in progress
    }
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
optional<pair<bool, int>> TLSSocket::on_yield(EventType event) {
    auto res = get<ChildTaskCompletion>(event);
    if (res.return_code <= 0) {
        return pair{true, res.return_code};
    }

    if (res.task_id == task_bytes_read_ && !task_bytes_r_fin_) {
        task_bytes_r_fin_ = true;
        e_read_s_ += res.return_code;
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

string TLSSocket::get_info() const {
    return "TLS socket adaptor";
}

pair<size_t&, char*> TLSSocket::get_e_read() {
    return {e_read_s_, e_read_};
}

optional<pair<bool, int>> TLSSocket::handle_handshake() {
    int ec = server_ ? SSL_accept(ssl_()) : SSL_connect(ssl_());
    if (SSL_get_error(ssl_(), ec) == SSL_ERROR_WANT_READ) {
        if (BIO_ctrl_pending(writebuf_)) {
            arm_write();
        } else {
            arm_read();
        }
    } else if (SSL_get_error(ssl_(), ec) != SSL_ERROR_NONE) {
        ERR_print_errors_fp(stderr);
        state_ = TLSState::Failed;
        return pair{true, -1};
    } else {
        state_ = TLSState::Full;
    }
    return nullopt;
}

optional<pair<bool, int>> TLSSocket::handle_read() {
    if (state_ == TLSState::WaitHello) {
        auto res = handle_handshake();
        if (res) return res;
    }

    if (state_ == TLSState::Full) { do {
        size_t readbytes = 0;
        int ret = SSL_read_ex(ssl_(), u_read_ + u_read_p_, u_readlen_ - u_read_p_, &readbytes);
        u_read_p_ += readbytes;

        // If bytes are read, it will not have an error
        switch (SSL_get_error(ssl_(), ret)) {
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
                } else if (!e_read_s_) {
                    arm_read();
                    return nullopt;
                }
                break; // Obtain remaining bytes
            default:
                ERR_print_errors_fp(stderr);
                return pair{true, -1};
        }
    } while (true);}

    return nullopt;
}

optional<pair<bool, int>> TLSSocket::handle_write() {
    if (state_ == TLSState::WaitHello) {
        auto res = handle_handshake();
        if (res) return res;
    }

    if (state_ == TLSState::Full) {
        size_t numread = 0;
        int ret = SSL_write_ex(ssl_(), u_write_ + u_write_p_, min(u_writelen_ - u_write_p_, MAXUFRAMELENGTH), &numread);
        u_write_p_ += numread;

        switch (SSL_get_error(ssl_(), ret)) {
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

void TLSSocket::arm_read() {
    if (task_bytes_r_fin_) {
        uint64_t taskid = c(&ReadWriteBytes::read, e_read_, sizeof(e_read_) - e_read_s_, false);
        task_bytes_read_ = taskid;
        task_bytes_r_fin_ = false;
    } else {
        attach(task_bytes_read_);
    }
}

void TLSSocket::arm_write() {
    if (task_bytes_w_fin_) {
        BIO_read_ex(writebuf_, e_write_, sizeof(e_write_), &e_writelen_);
        uint64_t taskid = c(&ReadWriteBytes::write, e_write_, e_writelen_);
        task_bytes_write_ = taskid;
        task_bytes_w_fin_ = false;
    } else {
        attach(task_bytes_write_);
    }
}

// Openssl callback implementation

int osslcb_read(BIO* b, char* dat, size_t len, size_t *written) {
    TLSSocket* ev = (TLSSocket*) BIO_get_data(b);

    auto [size, ptr] = ev->get_e_read();

    if (!size) {
        *written = 0;
        BIO_set_retry_read(b);
        return -1;
    }

    size_t towrite = min(size, len);
    memcpy(dat, ptr, towrite);
    *written = towrite;
    size -= towrite;
    memmove(ptr, ptr + towrite, size);
    return 1;
}

long osslcb_ctrl(BIO *, int cmd, long, void *) {
    if (cmd == BIO_CTRL_FLUSH) {
        return 1;
    }
    return 0;
}
