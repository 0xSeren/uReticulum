#include "ureticulum/cryptography/hashes.h"

#include <mbedtls/sha256.h>
#include <mbedtls/sha512.h>

namespace RNS { namespace Cryptography {

Bytes sha256(const Bytes& data) {
    Bytes hash;
    uint8_t* out = hash.writable(32);
    mbedtls_sha256(data.data(), data.size(), out, 0);
    return hash;
}

Bytes sha512(const Bytes& data) {
    Bytes hash;
    uint8_t* out = hash.writable(64);
    mbedtls_sha512(data.data(), data.size(), out, 0);
    return hash;
}

}}
