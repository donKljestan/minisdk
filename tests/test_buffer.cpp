#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "minisdk/buffer.hpp"

using minisdk::Buffer;

TEST_CASE("default constructed buffer owns nothing", "[buffer]") {
    Buffer b;
    REQUIRE(b.getSize() == 0);
}

TEST_CASE("sized buffer is value-initialised", "[buffer]") {
    Buffer b(16);
    REQUIRE(b.getSize() == 16);
    for (size_t i = 0; i < b.getSize(); ++i) {
        REQUIRE(b[i] == 0); // reading untouched bytes must be defined, not indeterminate
    }
}

TEST_CASE("copy is deep, not shared", "[buffer]") {
    Buffer a(4);
    a[0] = 7;

    Buffer copy(a);
    copy[0] = 9;

    REQUIRE(a[0] == 7);
    REQUIRE(copy[0] == 9);
    REQUIRE(a.getSize() == copy.getSize());
}

TEST_CASE("copy assignment survives self-assignment", "[buffer]") {
    Buffer a(4);
    a[1] = 3;

    Buffer& alias = a; // via a reference, otherwise the compiler rejects the obvious self-assign
    a = alias;

    REQUIRE(a.getSize() == 4);
    REQUIRE(a[1] == 3);
}

TEST_CASE("move transfers ownership and empties the source", "[buffer]") {
    Buffer a(8);
    a[0] = 5;

    SECTION("move construction") {
        Buffer moved(std::move(a));
        REQUIRE(moved.getSize() == 8);
        REQUIRE(moved[0] == 5);
        REQUIRE(a.getSize() == 0);
    }

    SECTION("move assignment") {
        Buffer moved(1);
        moved = std::move(a);
        REQUIRE(moved.getSize() == 8);
        REQUIRE(moved[0] == 5);
        REQUIRE(a.getSize() == 0);
    }
}

TEST_CASE("equality compares size and content", "[buffer]") {
    Buffer a(3);
    Buffer b(3);
    REQUIRE(a == b);

    b[2] = 1;
    REQUIRE_FALSE(a == b);

    Buffer shorter(2);
    REQUIRE_FALSE(a == shorter);
}

TEST_CASE("append concatenates both payloads", "[buffer]") {
    Buffer a(2);
    a[0] = 1;
    a[1] = 2;

    Buffer b(2);
    b[0] = 3;
    b[1] = 4;

    a += b;

    REQUIRE(a.getSize() == 4);
    REQUIRE(a[0] == 1);
    REQUIRE(a[2] == 3);
    REQUIRE(a[3] == 4);
}

TEST_CASE("appending a buffer to itself does not read freed memory", "[buffer]") {
    Buffer a(2);
    a[0] = 1;
    a[1] = 2;

    a += a;

    REQUIRE(a.getSize() == 4);
    REQUIRE(a[0] == 1);
    REQUIRE(a[2] == 1);
    REQUIRE(a[3] == 2);
}
