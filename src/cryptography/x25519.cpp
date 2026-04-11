#include "ureticulum/cryptography/x25519.h"

#include <stdexcept>

#include <monocypher.h>

#include "ureticulum/cryptography/random.h"

namespace RNS { namespace Cryptography {

X25519PrivateKey::X25519PrivateKey(const Bytes& sk) {
    if (sk.empty()) {
        _private = Cryptography::random(32);
    } else {
        if (sk.size() != 32) throw std::invalid_argument("X25519 private key must be 32 bytes");
        _private = sk;
    }
    Bytes pub_buf;
    crypto_x25519_public_key(pub_buf.writable(32), _private.data());
    _public = pub_buf;
}

Bytes X25519PrivateKey::exchange(const Bytes& peer_public_key) const {
    if (peer_public_key.size() != 32) throw std::invalid_argument("X25519 peer key must be 32 bytes");
    Bytes shared;
    crypto_x25519(shared.writable(32), _private.data(), peer_public_key.data());
    return shared;
}

}}
