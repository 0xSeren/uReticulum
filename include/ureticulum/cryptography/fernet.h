#pragma once

#include <memory>

#include "ureticulum/bytes.h"
#include "ureticulum/cryptography/random.h"

namespace RNS { namespace Cryptography {

    /* Reticulum's stripped-down Fernet: no version byte, no timestamp.
     * 32-byte key split into 16 signing + 16 encryption (AES-128-CBC). */
    class Fernet {
    public:
        using Ptr = std::shared_ptr<Fernet>;

        static inline Bytes generate_key() { return random(32); }

        explicit Fernet(const Bytes& key);

        bool  verify_hmac(const Bytes& token) const;
        Bytes encrypt(const Bytes& data) const;
        Bytes decrypt(const Bytes& token) const;

    private:
        Bytes _signing_key;
        Bytes _encryption_key;
    };

}}
