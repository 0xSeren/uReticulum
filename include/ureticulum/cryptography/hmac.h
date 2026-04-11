#pragma once

#include <stdexcept>

#include "ureticulum/bytes.h"

namespace RNS { namespace Cryptography {

    class HMAC {
    public:
        enum Digest {
            DIGEST_NONE,
            DIGEST_SHA256,
            DIGEST_SHA512,
        };

        HMAC(const Bytes& key, const Bytes& msg = {Bytes::NONE}, Digest digest = DIGEST_SHA256);
        ~HMAC();

        HMAC(const HMAC&)            = delete;
        HMAC& operator=(const HMAC&) = delete;

        void  update(const Bytes& msg);
        Bytes digest();

    private:
        Digest _digest;
        void*  _ctx;  /* mbedtls_md_context_t* — opaque to keep mbedtls headers out of users */
    };

    /* One-shot HMAC. */
    Bytes hmac(const Bytes& key, const Bytes& msg, HMAC::Digest digest = HMAC::DIGEST_SHA256);

}}
