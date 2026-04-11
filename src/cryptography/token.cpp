#include "ureticulum/cryptography/token.h"

#include <stdexcept>
#include <string>

#include "ureticulum/cryptography/aes.h"
#include "ureticulum/cryptography/hmac.h"
#include "ureticulum/cryptography/pkcs7.h"

namespace TM = RNS::Type::Cryptography::Token;

namespace RNS { namespace Cryptography {

Bytes Token::generate_key(Mode mode) {
    if (mode == TM::MODE_AES_128_CBC) return random(32);
    if (mode == TM::MODE_AES_256_CBC) return random(64);
    throw std::invalid_argument("Invalid token mode: " + std::to_string(mode));
}

Token::Token(const Bytes& key, Mode mode) : _mode(TM::MODE_AES_256_CBC) {
    if (key.empty()) throw std::invalid_argument("Token key cannot be empty");

    if (mode != TM::MODE_AES) throw std::invalid_argument("Invalid token mode: " + std::to_string(mode));

    if (key.size() == 32) {
        _mode = TM::MODE_AES_128_CBC;
        _signing_key    = key.left(16);
        _encryption_key = key.mid(16);
    } else if (key.size() == 64) {
        _mode = TM::MODE_AES_256_CBC;
        _signing_key    = key.left(32);
        _encryption_key = key.mid(32);
    } else {
        throw std::invalid_argument("Token key must be 256 or 512 bits, not " + std::to_string(key.size() * 8));
    }
}

bool Token::verify_hmac(const Bytes& token) const {
    if (token.size() <= 32)
        throw std::invalid_argument("Cannot verify HMAC on token of only " + std::to_string(token.size()) + " bytes");
    Bytes received = token.right(32);
    Bytes expected = hmac(_signing_key, token.left(token.size() - 32));
    return received == expected;
}

Bytes Token::encrypt(const Bytes& data) const {
    Bytes iv = random(16);
    Bytes ciphertext;
    Bytes padded = PKCS7::pad(data);

    if (_mode == TM::MODE_AES_128_CBC)      ciphertext = AES_128_CBC::encrypt(padded, _encryption_key, iv);
    else if (_mode == TM::MODE_AES_256_CBC) ciphertext = AES_256_CBC::encrypt(padded, _encryption_key, iv);
    else throw std::invalid_argument("Invalid token mode " + std::to_string(_mode));

    Bytes signed_parts = iv + ciphertext;
    return signed_parts + hmac(_signing_key, signed_parts);
}

Bytes Token::decrypt(const Bytes& token) const {
    if (token.size() < 48)
        throw std::invalid_argument("Cannot decrypt token of only " + std::to_string(token.size()) + " bytes");
    if (!verify_hmac(token))
        throw std::invalid_argument("Token HMAC was invalid");

    Bytes iv         = token.left(16);
    Bytes ciphertext = token.mid(16, token.size() - 48);

    if (_mode == TM::MODE_AES_128_CBC) return PKCS7::unpad(AES_128_CBC::decrypt(ciphertext, _encryption_key, iv));
    if (_mode == TM::MODE_AES_256_CBC) return PKCS7::unpad(AES_256_CBC::decrypt(ciphertext, _encryption_key, iv));
    throw std::invalid_argument("Invalid token mode " + std::to_string(_mode));
}

}}
