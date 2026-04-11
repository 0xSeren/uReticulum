#include "doctest.h"
#include "ureticulum/bytes.h"

using RNS::Bytes;

TEST_CASE("Bytes default-constructed is empty") {
    Bytes b;
    CHECK(b.size() == 0);
    CHECK(b.empty());
    CHECK_FALSE((bool)b);
}

TEST_CASE("Bytes from chunk round-trip") {
    const uint8_t data[] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x42};
    Bytes b(data, sizeof(data));
    CHECK(b.size() == sizeof(data));
    CHECK(memcmp(b.data(), data, sizeof(data)) == 0);
}

TEST_CASE("Bytes from C string is exact bytes") {
    Bytes b("hello");
    CHECK(b.size() == 5);
    CHECK(b.toString() == "hello");
}

TEST_CASE("Bytes append concatenates") {
    Bytes a("foo");
    a.append("bar");
    CHECK(a.toString() == "foobar");
}

TEST_CASE("Bytes operator+ produces new value") {
    Bytes a("ab"), b("cd");
    Bytes c = a + b;
    CHECK(c.toString() == "abcd");
    CHECK(a.toString() == "ab");
    CHECK(b.toString() == "cd");
}

TEST_CASE("Bytes copy is copy-on-write") {
    Bytes a("shared");
    Bytes b = a;
    CHECK(b.toString() == "shared");
    b.append("!");
    CHECK(a.toString() == "shared");
    CHECK(b.toString() == "shared!");
}

TEST_CASE("Bytes equality and ordering") {
    Bytes a("abc"), b("abc"), c("abd");
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
    CHECK(c > a);
}

TEST_CASE("Bytes hex round-trip") {
    Bytes a;
    a.assignHex("deadbeef");
    CHECK(a.size() == 4);
    CHECK(a.toHex() == "deadbeef");
    CHECK(a.toHex(true) == "DEADBEEF");
}

TEST_CASE("Bytes mid/left/right slicing") {
    Bytes b("0123456789");
    CHECK(b.mid(2, 4).toString() == "2345");
    CHECK(b.mid(7).toString() == "789");
    CHECK(b.left(3).toString() == "012");
    CHECK(b.right(3).toString() == "789");
}

TEST_CASE("Bytes find substring") {
    Bytes b("hello world");
    CHECK(b.find("world") == 6);
    CHECK(b.find("xyz") == -1);
}

TEST_CASE("Bytes resize grows and truncates") {
    Bytes b("hello");
    b.resize(3);
    CHECK(b.toString() == "hel");
    b.resize(5);
    CHECK(b.size() == 5);
}

TEST_CASE("Bytes index operator throws on overflow") {
    Bytes b("ab");
    CHECK(b[0] == 'a');
    CHECK(b[1] == 'b');
    CHECK_THROWS_AS((void)b[5], std::out_of_range);
}
