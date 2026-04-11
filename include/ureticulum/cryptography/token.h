#pragma once

#include <memory>

#include "ureticulum/bytes.h"
#include "ureticulum/cryptography/random.h"
#include "ureticulum/type.h"

namespace RNS { namespace Cryptography {

    /* Token: same construction as Fernet, but with AES-128-CBC or AES-256-CBC
     * selectable via key size (32 bytes → AES-128, 64 bytes → AES-256). */
    class Token {
    public:
        using Ptr = std::shared_ptr<Token>;
        using Mode = RNS::Type::Cryptography::Token::token_mode;

        static Bytes generate_key(Mode mode = RNS::Type::Cryptography::Token::MODE_AES_256_CBC);

        explicit Token(const Bytes& key, Mode mode = RNS::Type::Cryptography::Token::MODE_AES);

        bool  verify_hmac(const Bytes& token) const;
        Bytes encrypt(const Bytes& data) const;
        Bytes decrypt(const Bytes& token) const;

    private:
        Mode  _mode;
        Bytes _signing_key;
        Bytes _encryption_key;
    };

}}
