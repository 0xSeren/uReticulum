#pragma once

#include <memory>

#include "ureticulum/bytes.h"

namespace RNS { namespace Cryptography {

    class Ed25519PublicKey {
    public:
        using Ptr = std::shared_ptr<Ed25519PublicKey>;

        explicit Ed25519PublicKey(const Bytes& public_key) : _public(public_key) {}

        static Ptr   from_public_bytes(const Bytes& public_key) { return std::make_shared<Ed25519PublicKey>(public_key); }
        const Bytes& public_bytes() const { return _public; }

        bool verify(const Bytes& signature, const Bytes& message) const;

    private:
        Bytes _public;
    };

    class Ed25519PrivateKey {
    public:
        using Ptr = std::shared_ptr<Ed25519PrivateKey>;

        explicit Ed25519PrivateKey(const Bytes& private_key);

        static Ptr generate()                              { return std::make_shared<Ed25519PrivateKey>(Bytes{Bytes::NONE}); }
        static Ptr from_private_bytes(const Bytes& seed)   { return std::make_shared<Ed25519PrivateKey>(seed); }

        const Bytes& private_bytes() const { return _seed; }
        Ed25519PublicKey::Ptr public_key() const { return Ed25519PublicKey::from_public_bytes(_public); }

        Bytes sign(const Bytes& message) const;

    private:
        Bytes _seed;     /* 32-byte Ed25519 seed */
        Bytes _public;   /* 32-byte derived public key */
    };

}}
