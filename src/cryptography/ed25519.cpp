#include "ureticulum/cryptography/ed25519.h"

#include <stdexcept>
#include <string.h>

#include <monocypher-ed25519.h>

#include "ureticulum/cryptography/random.h"

namespace RNS { namespace Cryptography {

bool Ed25519PublicKey::verify(const Bytes& signature, const Bytes& message) const {
    if (signature.size() != 64 || _public.size() != 32) return false;
    return crypto_ed25519_check(signature.data(), _public.data(),
                                message.data(), message.size()) == 0;
}

Ed25519PrivateKey::Ed25519PrivateKey(const Bytes& seed) {
    if (seed.empty()) {
        _seed = Cryptography::random(32);
    } else {
        if (seed.size() != 32) throw std::invalid_argument("Ed25519 seed must be 32 bytes");
        _seed = seed;
    }

    /* monocypher overwrites its seed argument, so feed it a stack copy. */
    uint8_t seed_buf[32];
    memcpy(seed_buf, _seed.data(), 32);
    uint8_t expanded[64];
    uint8_t pub[32];
    crypto_ed25519_key_pair(expanded, pub, seed_buf);
    _public = Bytes(pub, 32);

    memset(seed_buf, 0, sizeof(seed_buf));
    memset(expanded, 0, sizeof(expanded));
}

Bytes Ed25519PrivateKey::sign(const Bytes& message) const {
    uint8_t seed_buf[32];
    memcpy(seed_buf, _seed.data(), 32);
    uint8_t expanded[64];
    uint8_t pub[32];
    crypto_ed25519_key_pair(expanded, pub, seed_buf);

    uint8_t sig_buf[64];
    crypto_ed25519_sign(sig_buf, expanded, message.data(), message.size());

    memset(seed_buf, 0, sizeof(seed_buf));
    memset(expanded, 0, sizeof(expanded));
    return Bytes(sig_buf, 64);
}

}}
