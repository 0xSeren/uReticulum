#pragma once

#include "ureticulum/bytes.h"

namespace RNS { namespace Cryptography {

    class AES_128_CBC {
    public:
        static Bytes encrypt(const Bytes& plaintext, const Bytes& key, const Bytes& iv);
        static Bytes decrypt(const Bytes& ciphertext, const Bytes& key, const Bytes& iv);
    };

    class AES_256_CBC {
    public:
        static Bytes encrypt(const Bytes& plaintext, const Bytes& key, const Bytes& iv);
        static Bytes decrypt(const Bytes& ciphertext, const Bytes& key, const Bytes& iv);
    };

}}
