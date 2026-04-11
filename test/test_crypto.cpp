#include "doctest.h"

#include "ureticulum/cryptography/aes.h"
#include "ureticulum/cryptography/ed25519.h"
#include "ureticulum/cryptography/fernet.h"
#include "ureticulum/cryptography/hashes.h"
#include "ureticulum/cryptography/hkdf.h"
#include "ureticulum/cryptography/hmac.h"
#include "ureticulum/cryptography/random.h"
#include "ureticulum/cryptography/token.h"
#include "ureticulum/cryptography/x25519.h"

using namespace RNS;
using namespace RNS::Cryptography;

static Bytes hex(const char* s) { Bytes b; b.assignHex(s); return b; }

/* ---------- SHA-256 / SHA-512 (NIST FIPS 180-4 sample vectors) ---------- */

TEST_CASE("SHA-256 known answers") {
    /* "abc" */
    CHECK(sha256(Bytes("abc")).toHex() ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    /* empty */
    CHECK(sha256(Bytes("")).toHex() ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    /* 56-byte block-boundary case */
    CHECK(sha256(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")).toHex() ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA-512 known answers") {
    /* "abc" */
    CHECK(sha512(Bytes("abc")).toHex() ==
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
          "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    /* empty */
    CHECK(sha512(Bytes("")).toHex() ==
          "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
          "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
}

/* ---------- HMAC-SHA256 (RFC 4231 test case 1) ---------- */

TEST_CASE("HMAC-SHA256 RFC 4231 test 1") {
    Bytes key  = hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    Bytes data = Bytes("Hi There");
    Bytes mac  = hmac(key, data, HMAC::DIGEST_SHA256);
    CHECK(mac.toHex() == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST_CASE("HMAC-SHA256 RFC 4231 test 2") {
    Bytes key  = Bytes("Jefe");
    Bytes data = Bytes("what do ya want for nothing?");
    Bytes mac  = hmac(key, data, HMAC::DIGEST_SHA256);
    CHECK(mac.toHex() == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

/* ---------- HKDF-SHA256 (RFC 5869 test case 1) ---------- */

TEST_CASE("HKDF-SHA256 RFC 5869 test 1") {
    Bytes ikm  = hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    Bytes salt = hex("000102030405060708090a0b0c");
    Bytes info = hex("f0f1f2f3f4f5f6f7f8f9");
    Bytes okm  = hkdf(42, ikm, salt, info);
    CHECK(okm.toHex() == "3cb25f25faacd57a90434f64d0362f2a"
                         "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                         "34007208d5b887185865");
}

/* ---------- AES-256-CBC (NIST SP 800-38A appendix F.2.5 sample) ---------- */

TEST_CASE("AES-256-CBC NIST SP 800-38A F.2.5") {
    Bytes key = hex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    Bytes iv  = hex("000102030405060708090a0b0c0d0e0f");
    Bytes pt  = hex("6bc1bee22e409f96e93d7e117393172a"
                    "ae2d8a571e03ac9c9eb76fac45af8e51"
                    "30c81c46a35ce411e5fbc1191a0a52ef"
                    "f69f2445df4f9b17ad2b417be66c3710");
    Bytes ct = AES_256_CBC::encrypt(pt, key, iv);
    CHECK(ct.toHex() == "f58c4c04d6e5f1ba779eabfb5f7bfbd6"
                        "9cfc4e967edb808d679f777bc6702c7d"
                        "39f23369a9d9bacfa530e26304231461"
                        "b2eb05e2c39be9fcda6c19078c6a9d1b");
    Bytes round = AES_256_CBC::decrypt(ct, key, iv);
    CHECK(round == pt);
}

/* ---------- Ed25519 (RFC 8032 test 1) ---------- */

TEST_CASE("Ed25519 RFC 8032 test 1") {
    Bytes seed = hex("9d61b19deffd5a60ba844af492ec2cc4"
                     "4449c5697b326919703bac031cae7f60");
    auto sk    = Ed25519PrivateKey::from_private_bytes(seed);
    auto pk    = sk->public_key();
    CHECK(pk->public_bytes().toHex() ==
          "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

    Bytes msg;  /* empty message */
    Bytes sig = sk->sign(msg);
    CHECK(sig.toHex() == "e5564300c360ac729086e2cc806e828a"
                         "84877f1eb8e5d974d873e06522490155"
                         "5fb8821590a33bacc61e39701cf9b46b"
                         "d25bf5f0595bbe24655141438e7a100b");
    CHECK(pk->verify(sig, msg));
}

TEST_CASE("Ed25519 RFC 8032 test 2") {
    Bytes seed = hex("4ccd089b28ff96da9db6c346ec114e0f"
                     "5b8a319f35aba624da8cf6ed4fb8a6fb");
    auto sk = Ed25519PrivateKey::from_private_bytes(seed);
    Bytes msg = hex("72");
    Bytes sig = sk->sign(msg);
    CHECK(sig.toHex() == "92a009a9f0d4cab8720e820b5f642540"
                         "a2b27b5416503f8fb3762223ebdb69da"
                         "085ac1e43e15996e458f3613d0f11d8c"
                         "387b2eaeb4302aeeb00d291612bb0c00");
    CHECK(sk->public_key()->verify(sig, msg));
}

TEST_CASE("Ed25519 verify rejects tampered signature") {
    auto sk  = Ed25519PrivateKey::generate();
    auto pk  = sk->public_key();
    Bytes msg = Bytes("hello");
    Bytes sig = sk->sign(msg);

    /* Flip a byte. */
    Bytes bad = sig;
    bad[10] ^= 0x01;
    CHECK_FALSE(pk->verify(bad, msg));
    CHECK(pk->verify(sig, msg));
}

/* ---------- X25519 (RFC 7748 §6.1) ---------- */

TEST_CASE("X25519 RFC 7748 6.1") {
    Bytes alice_sk = hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    Bytes bob_sk   = hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    Bytes alice_pk_expected = hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    Bytes bob_pk_expected   = hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    Bytes shared_expected   = hex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");

    auto alice = X25519PrivateKey::from_private_bytes(alice_sk);
    auto bob   = X25519PrivateKey::from_private_bytes(bob_sk);
    CHECK(alice->public_key()->public_bytes() == alice_pk_expected);
    CHECK(bob->public_key()->public_bytes()   == bob_pk_expected);
    CHECK(alice->exchange(bob->public_key()->public_bytes()) == shared_expected);
    CHECK(bob->exchange(alice->public_key()->public_bytes()) == shared_expected);
}

/* ---------- Fernet round-trip ---------- */

TEST_CASE("Fernet round-trip") {
    Bytes key = Fernet::generate_key();
    Fernet f(key);
    Bytes plaintext = Bytes("hello reticulum");
    Bytes ct = f.encrypt(plaintext);
    CHECK(ct.size() >= 48);
    Bytes pt = f.decrypt(ct);
    CHECK(pt == plaintext);
}

TEST_CASE("Fernet HMAC tamper detection") {
    Bytes key = Fernet::generate_key();
    Fernet f(key);
    Bytes ct = f.encrypt(Bytes("secret"));
    ct[20] ^= 0x01;
    CHECK_THROWS_AS((void)f.decrypt(ct), std::invalid_argument);
}

/* ---------- Token round-trip (AES-256-CBC) ---------- */

TEST_CASE("Token AES-256 round-trip") {
    Bytes key = Token::generate_key(RNS::Type::Cryptography::Token::MODE_AES_256_CBC);
    CHECK(key.size() == 64);
    Token t(key);
    Bytes pt = Bytes("a longer message that crosses block boundaries for sure");
    Bytes ct = t.encrypt(pt);
    Bytes round = t.decrypt(ct);
    CHECK(round == pt);
}

TEST_CASE("Token AES-128 round-trip") {
    Bytes key = Token::generate_key(RNS::Type::Cryptography::Token::MODE_AES_128_CBC);
    CHECK(key.size() == 32);
    Token t(key);
    Bytes pt = Bytes("aes-128 mode");
    Bytes ct = t.encrypt(pt);
    Bytes round = t.decrypt(ct);
    CHECK(round == pt);
}

/* ---------- Random ---------- */

TEST_CASE("Random produces requested length") {
    Bytes a = Cryptography::random(32);
    Bytes b = Cryptography::random(32);
    CHECK(a.size() == 32);
    CHECK(b.size() == 32);
    CHECK(a != b);
}
