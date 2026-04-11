#include "ureticulum/cryptography/fernet.h"

#include <stdexcept>
#include <string>

#include "ureticulum/cryptography/aes.h"
#include "ureticulum/cryptography/hmac.h"
#include "ureticulum/cryptography/pkcs7.h"

namespace RNS { namespace Cryptography {

Fernet::Fernet(const Bytes& key) {
    if (key.empty())     throw std::invalid_argument("Fernet key cannot be empty");
    if (key.size() != 32) throw std::invalid_argument("Fernet key must be 32 bytes, not " + std::to_string(key.size()));
    _signing_key    = key.left(16);
    _encryption_key = key.mid(16);
}

bool Fernet::verify_hmac(const Bytes& token) const {
    if (token.size() <= 32)
        throw std::invalid_argument("Cannot verify HMAC on token of only " + std::to_string(token.size()) + " bytes");
    Bytes received = token.right(32);
    Bytes expected = hmac(_signing_key, token.left(token.size() - 32));
    return received == expected;
}

Bytes Fernet::encrypt(const Bytes& data) const {
    Bytes iv         = random(16);
    Bytes ciphertext = AES_128_CBC::encrypt(PKCS7::pad(data), _encryption_key, iv);
    Bytes signed_parts = iv + ciphertext;
    return signed_parts + hmac(_signing_key, signed_parts);
}

Bytes Fernet::decrypt(const Bytes& token) const {
    if (token.size() < 48)
        throw std::invalid_argument("Cannot decrypt token of only " + std::to_string(token.size()) + " bytes");
    if (!verify_hmac(token))
        throw std::invalid_argument("Fernet token HMAC was invalid");

    Bytes iv         = token.left(16);
    Bytes ciphertext = token.mid(16, token.size() - 48);
    return PKCS7::unpad(AES_128_CBC::decrypt(ciphertext, _encryption_key, iv));
}

}}
