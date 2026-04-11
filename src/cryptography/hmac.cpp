#include "ureticulum/cryptography/hmac.h"

#include <mbedtls/md.h>

namespace RNS { namespace Cryptography {

static const mbedtls_md_info_t* md_info_for(HMAC::Digest d) {
    switch (d) {
    case HMAC::DIGEST_SHA256: return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    case HMAC::DIGEST_SHA512: return mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    default:                  return nullptr;
    }
}

HMAC::HMAC(const Bytes& key, const Bytes& msg, Digest digest) : _digest(digest), _ctx(nullptr) {
    const mbedtls_md_info_t* info = md_info_for(digest);
    if (!info) throw std::invalid_argument("HMAC: unsupported digest");

    auto* ctx = new mbedtls_md_context_t;
    mbedtls_md_init(ctx);
    if (mbedtls_md_setup(ctx, info, 1) != 0) {
        mbedtls_md_free(ctx);
        delete ctx;
        throw std::runtime_error("HMAC: mbedtls_md_setup failed");
    }
    if (mbedtls_md_hmac_starts(ctx, key.data(), key.size()) != 0) {
        mbedtls_md_free(ctx);
        delete ctx;
        throw std::runtime_error("HMAC: mbedtls_md_hmac_starts failed");
    }
    _ctx = ctx;

    if (msg) update(msg);
}

HMAC::~HMAC() {
    if (_ctx) {
        auto* ctx = static_cast<mbedtls_md_context_t*>(_ctx);
        mbedtls_md_free(ctx);
        delete ctx;
    }
}

void HMAC::update(const Bytes& msg) {
    if (msg.empty()) return;
    auto* ctx = static_cast<mbedtls_md_context_t*>(_ctx);
    mbedtls_md_hmac_update(ctx, msg.data(), msg.size());
}

Bytes HMAC::digest() {
    auto* ctx = static_cast<mbedtls_md_context_t*>(_ctx);
    size_t out_len = mbedtls_md_get_size(mbedtls_md_info_from_ctx(ctx));
    Bytes out;
    uint8_t* buf = out.writable(out_len);
    mbedtls_md_hmac_finish(ctx, buf);
    return out;
}

Bytes hmac(const Bytes& key, const Bytes& msg, HMAC::Digest digest) {
    HMAC h(key, msg, digest);
    return h.digest();
}

}}
