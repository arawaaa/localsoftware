#pragma once

#include <limits>
#include <memory>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "defs.h"
#include "event.h"

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

/* Class for managing TLS connections
 */

class TLSSocket : public Event {
public:
    TLSSocket(vector<shared_ptr<File>> file, bool server);

    ~TLSSocket();

    void openssl_setup();

    CallResponse read(uint64_t id, char* buf, size_t len, bool read_all = true);

    CallResponse write(uint64_t id, char* buf, size_t len);

    /**
     * @brief Handle the completion queue entry (CQE) result.
     */
    optional<pair<bool, int>> on_yield(EventType event) override;

    string get_info() const override;

    pair<size_t&, char*> get_e_read();

protected:
    uint64_t task_bytes_read_, task_bytes_write_;
    bool task_bytes_r_fin_ = true, task_bytes_w_fin_ = true;
    uint64_t task_read_, task_write_, task_write_2_ = numeric_limits<uint64_t>::max();
    bool server_;

    bool sticky_read_ = false;
    char* u_read_, *u_write_;
    size_t u_readlen_ = 0, u_writelen_ = 0, u_read_p_ = 0, u_write_p_ = 0;

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

    optional<pair<bool, int>> handle_handshake();

    optional<pair<bool, int>> handle_read();

    optional<pair<bool, int>> handle_write();

    void arm_read();

    void arm_write();
};
