#include "ureticulum/cryptography/aes.h"

#include <stdexcept>

#include <mbedtls/aes.h>

namespace RNS { namespace Cryptography {

static Bytes aes_cbc(const Bytes& input, const Bytes& key, const Bytes& iv,
                     unsigned int key_bits, int mode) {
    if (input.size() % 16 != 0) throw std::invalid_argument("AES-CBC: input not block-aligned");
    if (iv.size() != 16)        throw std::invalid_argument("AES-CBC: IV must be 16 bytes");
    if (key.size() * 8 != key_bits) throw std::invalid_argument("AES-CBC: key size mismatch");

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    int rc = (mode == MBEDTLS_AES_ENCRYPT)
                 ? mbedtls_aes_setkey_enc(&ctx, key.data(), key_bits)
                 : mbedtls_aes_setkey_dec(&ctx, key.data(), key_bits);
    if (rc != 0) { mbedtls_aes_free(&ctx); throw std::runtime_error("mbedtls_aes_setkey failed"); }

    /* mbedtls mutates the IV buffer; copy first. */
    uint8_t iv_buf[16];
    memcpy(iv_buf, iv.data(), 16);

    Bytes out;
    uint8_t* out_buf = out.writable(input.size());
    rc = mbedtls_aes_crypt_cbc(&ctx, mode, input.size(), iv_buf, input.data(), out_buf);
    mbedtls_aes_free(&ctx);
    if (rc != 0) throw std::runtime_error("mbedtls_aes_crypt_cbc failed");
    return out;
}

Bytes AES_128_CBC::encrypt(const Bytes& pt, const Bytes& key, const Bytes& iv) { return aes_cbc(pt, key, iv, 128, MBEDTLS_AES_ENCRYPT); }
Bytes AES_128_CBC::decrypt(const Bytes& ct, const Bytes& key, const Bytes& iv) { return aes_cbc(ct, key, iv, 128, MBEDTLS_AES_DECRYPT); }
Bytes AES_256_CBC::encrypt(const Bytes& pt, const Bytes& key, const Bytes& iv) { return aes_cbc(pt, key, iv, 256, MBEDTLS_AES_ENCRYPT); }
Bytes AES_256_CBC::decrypt(const Bytes& ct, const Bytes& key, const Bytes& iv) { return aes_cbc(ct, key, iv, 256, MBEDTLS_AES_DECRYPT); }

}}
