#include "async_handler.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <variant>
#include <vector>
#include <functional>
#include <tuple>
#include <utility>
#include <optional>
#include <typeindex>
#include <queue>
#include <liburing.h>
#include <ranges>
#include <functional>
#include <iostream>
#include <thread>

#include <oneapi/tbb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "event.h"
#include "defs.h"
#include "thread_data.h"
#include "async_thread_shared.h"

using namespace std;

class ThreadData;

void AsyncHandler::start() {
    vector<thread> threads;
    for (size_t i = 0; i < per_thread_data.size(); i++) {
        private_per_thread_data.emplace_back(ThreadData(per_thread_data, blocks, i));
        threads.emplace_back(&ThreadData::run, &private_per_thread_data[i]);
    }
    for (auto &t : threads) {
        t.join();
    }
}

SSL_CTX* AsyncHandler::get_tls_ctx() {
    return tls_ctx_;
}

uint64_t AsyncHandler::get_proc_id() {
    if (r_proc_id_curr == r_proc_id_base + 1000 || !r_p_init) {
        r_proc_id_base = r_proc_id_curr = 1000 * blocks.proc_block_++;
        r_p_init = true;
    }
    return r_proc_id_curr++;
}

uint64_t AsyncHandler::get_obj_id() {
    if (r_obj_id_curr == r_obj_id_base + 1000 || !r_o_init) {
        r_obj_id_base = r_obj_id_curr = 1000 * blocks.obj_block_++;
        r_o_init = true;
    }
    return r_obj_id_curr++;
}

AsyncHandler::AsyncHandler(int ts) : per_thread_data(ts) {
    for (int i = 0; i < ts; i++) {
        private_per_thread_data.emplace_back(per_thread_data, blocks, i);
    }

    tls_ctx_ = SSL_CTX_new(TLS_server_method());
    if (tls_ctx_ == NULL) {
        throw runtime_error{"Unable to create libssl context"};
    }

    if (!SSL_CTX_set_min_proto_version(tls_ctx_, TLS1_2_VERSION)) {
        SSL_CTX_free(tls_ctx_);
        throw runtime_error{"Unable to set minimum TLS version to 1.2"};
    }

    auto opts = SSL_OP_IGNORE_UNEXPECTED_EOF | SSL_OP_NO_RENEGOTIATION | SSL_OP_CIPHER_SERVER_PREFERENCE;
    SSL_CTX_set_options(tls_ctx_, opts);

    if (SSL_CTX_use_certificate_chain_file(tls_ctx_, "/etc/letsencrypt/live/arnavrawat.xyz-0001/fullchain.pem") <= 0) {
        SSL_CTX_free(tls_ctx_);
        throw runtime_error{"Unable to load certificate chain"};
    }

    if (SSL_CTX_use_PrivateKey_file(tls_ctx_, "/etc/letsencrypt/live/arnavrawat.xyz-0001/privkey.pem", SSL_FILETYPE_PEM) <= 0) {
        SSL_CTX_free(tls_ctx_);
        throw runtime_error{"Unable to load private key. Check for certificate / key mismatch."};
    }

    // No session resumption for now

    SSL_CTX_set_verify(tls_ctx_, SSL_VERIFY_NONE, NULL);
};

AsyncHandler::~AsyncHandler() {
    SSL_CTX_free(tls_ctx_);
}
