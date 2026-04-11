#pragma once

#include <memory>

#include "ureticulum/bytes.h"

namespace RNS { namespace Cryptography {

    class X25519PublicKey {
    public:
        using Ptr = std::shared_ptr<X25519PublicKey>;

        explicit X25519PublicKey(const Bytes& public_key) : _public(public_key) {}

        static Ptr   from_public_bytes(const Bytes& public_key) { return std::make_shared<X25519PublicKey>(public_key); }
        const Bytes& public_bytes() const { return _public; }

    private:
        Bytes _public;
    };

    class X25519PrivateKey {
    public:
        using Ptr = std::shared_ptr<X25519PrivateKey>;

        explicit X25519PrivateKey(const Bytes& private_key);

        static Ptr generate()                            { return std::make_shared<X25519PrivateKey>(Bytes{Bytes::NONE}); }
        static Ptr from_private_bytes(const Bytes& sk)   { return std::make_shared<X25519PrivateKey>(sk); }

        const Bytes&         private_bytes() const { return _private; }
        X25519PublicKey::Ptr public_key()    const { return X25519PublicKey::from_public_bytes(_public); }

        /* Curve25519 ECDH. Returns the 32-byte raw shared secret. */
        Bytes exchange(const Bytes& peer_public_key) const;

    private:
        Bytes _private;
        Bytes _public;
    };

}}
