#include "doctest.h"
#include "ureticulum/cryptography/pkcs7.h"

using RNS::Bytes;
using RNS::Cryptography::PKCS7;

TEST_CASE("PKCS7 pad fills to block boundary") {
    Bytes b("YELLOW SUBMARINE");
    CHECK(b.size() == 16);
    Bytes padded = PKCS7::pad(b);
    CHECK(padded.size() == 32);
    /* Full block of padding when input is already aligned. */
    for (size_t i = 16; i < 32; ++i) CHECK(padded.data()[i] == 16);
}

TEST_CASE("PKCS7 pad short input") {
    Bytes b("hello");
    Bytes padded = PKCS7::pad(b);
    CHECK(padded.size() == 16);
    CHECK(padded.data()[15] == 11);
    CHECK(padded.data()[5]  == 11);
}

TEST_CASE("PKCS7 pad/unpad round-trip") {
    const char* messages[] = {"", "a", "hello world", "YELLOW SUBMARINE", "this is a longer message that spans multiple blocks"};
    for (auto* m : messages) {
        Bytes original(m);
        Bytes padded   = PKCS7::pad(original);
        CHECK(padded.size() % 16 == 0);
        Bytes restored = PKCS7::unpad(padded);
        CHECK(restored == original);
    }
}

TEST_CASE("PKCS7 unpad rejects invalid padding length") {
    Bytes bad("YELLOW SUBMARINE");
    /* Last byte 0x20 (32) > blocksize 16 — must throw. */
    bad[15] = 0x20;
    CHECK_THROWS_AS(PKCS7::unpad(bad), std::runtime_error);
}
