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
    : Event(file), socket_(file), server_(server)
{
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

pinned<int> TLSSocket::read(char* buf, size_t len, bool read_all) {
    size_t read_position = 0;
    if (state_ == TLSState::WaitHello && !(co_await handle_handshake()))
        co_return -1;

    while(read_position < len && (read_all || !read_position)) {
        size_t readbytes = 0;
        int ret = SSL_read_ex(ssl_(), buf + read_position, len - read_position, &readbytes);
        read_position += readbytes;

        // If bytes are read, it will not have an error
        switch (SSL_get_error(ssl_(), ret)) {
            case SSL_ERROR_WANT_READ:
                if (BIO_ctrl_pending(writebuf_) && !(co_await socket_write())) {
                    co_return -1;
                }
                if (!co_await socket_read()) {
                    co_return -1;
                }
                break;
            case SSL_ERROR_NONE:
                break; // Obtain remaining bytes
            default:
                ERR_print_errors_fp(stderr);
                co_return -1;
        }
    }

    co_return read_position;
}

pinned<int> TLSSocket::write(char* buf, size_t len) {
    size_t write_position = 0;
    u_write_ = buf;
    u_writelen_ = len;
    u_write_p_ = 0;

    if (state_ == TLSState::WaitHello && !(co_await handle_handshake()))
        co_return -1;

    while (write_position < len) {
        size_t numread = 0;
        int ret = SSL_write_ex(ssl_(), u_write_ + write_position, min(len - write_position, MAXUFRAMELENGTH), &numread);
        write_position += numread;

        switch (SSL_get_error(ssl_(), ret)) {
            case SSL_ERROR_WANT_READ:
                if (BIO_ctrl_pending(writebuf_) && !co_await socket_write()) {
                    co_return -1;
                } else if (!co_await socket_read()){
                    co_return -1;
                }
                break;
            case SSL_ERROR_NONE:
                if (BIO_ctrl_pending(writebuf_) && !co_await socket_write()) {
                    co_return -1;
                }
                break;
            default:
                ERR_print_errors_fp(stderr);
                co_return -1;
        }
    }

    co_return write_position;
}

pair<size_t&, char*> TLSSocket::get_e_read() {
    return {e_read_s_, e_read_};
}

pinned<bool> TLSSocket::handle_handshake() {
    while (state_ != TLSState::Full) {
        int ec = server_ ? SSL_accept(ssl_()) : SSL_connect(ssl_());
        if (SSL_get_error(ssl_(), ec) == SSL_ERROR_WANT_READ) {
            bool success = true;
            if (BIO_ctrl_pending(writebuf_)) {
                success = co_await socket_write();
            }

            if (!success)
                co_return false;

            success = co_await socket_read();
            if (!success)
                co_return false;
        } else if (SSL_get_error(ssl_(), ec) != SSL_ERROR_NONE) {
            ERR_print_errors_fp(stderr);
            state_ = TLSState::Failed;
            co_return false;
        } else {
            state_ = TLSState::Full;
        }
    }
    co_return true;
}

// TODO Make these functions shared tasks to allow full-duplex socket usage!

pinned<bool> TLSSocket::socket_read() {
    int reads = co_await socket_.read(e_read_, MAXFRAMELENGTH - e_read_s_, false);
    co_return reads > 0 && (e_read_s_ += reads);
}

pinned<bool> TLSSocket::socket_write() {
    BIO_read_ex(writebuf_, e_write_, MAXFRAMELENGTH , &e_writelen_);
    int written = co_await socket_.write(e_write_, e_writelen_);
    co_return written > 0;
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
