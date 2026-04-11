#include "doctest.h"
#include "ureticulum/memory.h"

using RNS::Utilities::Memory;

TEST_CASE("TLSF pool allocates and frees") {
    Memory::pool_info pi(64 * 1024);
    void* a = Memory::pool_malloc(pi, 128);
    void* b = Memory::pool_malloc(pi, 256);
    void* c = Memory::pool_malloc(pi, 1024);
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    CHECK(c != nullptr);
    CHECK(a != b);
    CHECK(b != c);
    Memory::pool_free(pi, a);
    Memory::pool_free(pi, b);
    Memory::pool_free(pi, c);
}

TEST_CASE("TLSF pool: zero-size allocation returns null") {
    Memory::pool_info pi(8 * 1024);
    CHECK(Memory::pool_malloc(pi, 0) == nullptr);
}

TEST_CASE("TLSF pool: many allocations reuse memory") {
    Memory::pool_info pi(64 * 1024);
    for (int i = 0; i < 1000; ++i) {
        void* p = Memory::pool_malloc(pi, 32);
        CHECK(p != nullptr);
        Memory::pool_free(pi, p);
    }
}
