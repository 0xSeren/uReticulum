#include "ureticulum/cryptography/hkdf.h"

#include <stdexcept>

#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>

namespace RNS { namespace Cryptography {

Bytes hkdf(size_t length, const Bytes& derive_from, const Bytes& salt, const Bytes& context) {
    if (length == 0) throw std::invalid_argument("Invalid output key length");
    if (derive_from.empty()) throw std::invalid_argument("Cannot derive key from empty input material");

    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    Bytes derived;
    uint8_t* out = derived.writable(length);

    int rc = mbedtls_hkdf(info,
                          salt.data(),    salt.size(),
                          derive_from.data(), derive_from.size(),
                          context.data(), context.size(),
                          out, length);
    if (rc != 0) throw std::runtime_error("mbedtls_hkdf failed");
    return derived;
}

}}
