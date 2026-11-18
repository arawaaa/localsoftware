#pragma once

#include <limits>
#include <memory>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "readwrite_bytes.h"
#include "defs.h"
#include "event.h"
#include "coro_pinned.h"

#define MAXFRAMELENGTH (size_t)(16 * 1024)
#define MAXUFRAMELENGTH (size_t)(16384 - 1024) // little bit of margin

using namespace std;

// Ossl callbacks

int osslcb_read(BIO* b, char* dat, size_t len, size_t *written);
long osslcb_ctrl(BIO *, int cmd, long, void *);

// Encapsulation for correct copy/move semantics
class SSLCtx {
    SSL* ssl_ = nullptr;
public:
    SSLCtx();

    SSLCtx(SSLCtx&) = delete;

    SSLCtx(SSLCtx&& other);

    SSLCtx& operator=(SSLCtx&& other);

    SSL* operator()();

    ~SSLCtx();
};

/*
 * Class for managing TLS connections
 *
 * This class is not thread-safe! All functions have to be called
 * from the same thread
 *
 * TODO make multiple-coroutine-reentrancy correct
 * will need mt_coro_pinned.h to be used for both handshake & regular read/writes
 */

class TLSSocket : public Event {
public:
    TLSSocket(vector<shared_ptr<File>> file, bool server);

    ~TLSSocket();

    TLSSocket(TLSSocket&& other) = default;
    TLSSocket& operator=(TLSSocket&& other) = default;

    pinned<int> read(char* buf, size_t len, bool read_all = true);

    pinned<int> write(char* buf, size_t len);

    pair<size_t&, char*> get_e_read();

protected:
    ReadWriteBytes socket_;
    uint64_t task_bytes_read_, task_bytes_write_;
    bool task_bytes_r_fin_ = true, task_bytes_w_fin_ = true;
    uint64_t task_read_, task_write_, task_write_2_ = numeric_limits<uint64_t>::max();
    bool server_;

    char* u_write_;
    size_t u_writelen_ = 0, u_write_p_ = 0;

    // TLS variables. Buffer procession for recv: recv -> encryptread -> ssl_read -> buffer_ -> parse
    enum TLSState {
        WaitHello,
        Full,
        Failed
    };

    SSLCtx ssl_;
    BIO *writebuf_, *readbuf_;
    // The write function will take care of the entire buffer
    size_t e_writelen_;
    char* e_read_, *e_write_;
    size_t e_read_s_ = 0, e_write_s_ = 0;
    TLSState state_;

    void openssl_setup();

    pinned<bool> handle_handshake();

    pinned<bool> socket_read();

    pinned<bool> socket_write();
};
