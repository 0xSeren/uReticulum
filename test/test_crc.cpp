#include "doctest.h"
#include "ureticulum/crc.h"

using RNS::Utilities::Crc;

TEST_CASE("Crc::crc32 known answers") {
    /* Reference values from zlib's crc32 (same polynomial / init / xorout). */
    CHECK(Crc::crc32(0, (const uint8_t*)"", 0)             == 0u);
    CHECK(Crc::crc32(0, (const uint8_t*)"a", 1)            == 0xe8b7be43u);
    CHECK(Crc::crc32(0, (const uint8_t*)"abc", 3)          == 0x352441c2u);
    CHECK(Crc::crc32(0, (const uint8_t*)"123456789", 9)    == 0xcbf43926u);
}

TEST_CASE("Crc::crc32 incremental matches one-shot") {
    const char* msg = "The quick brown fox jumps over the lazy dog";
    uint32_t one_shot = Crc::crc32(0, (const uint8_t*)msg, strlen(msg));
    uint32_t incr     = 0;
    for (size_t i = 0; i < strlen(msg); ++i)
        incr = Crc::crc32(incr, (uint8_t)msg[i]);
    CHECK(one_shot == incr);
}

TEST_CASE("Crc::crc32 null buffer is zero") {
    CHECK(Crc::crc32(0, (const uint8_t*)nullptr, 10) == 0u);
}
