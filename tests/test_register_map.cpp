#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "minisdk/RegisterMap.hpp"

using minisdk::RegisterMap;

namespace {

RegisterMap<uint32_t> makeMap() {
    RegisterMap<uint32_t> map;
    map.add("TX", 1);
    map.add("RX", 2);
    map.add("MTU", 1500);
    return map;
}

bool contains(const std::vector<std::string>& v, const std::string& name) {
    return std::find(v.begin(), v.end(), name) != v.end();
}

} // namespace

TEST_CASE("add rejects a duplicate register instead of ignoring it", "[registermap]") {
    RegisterMap<uint32_t> map;
    map.add("TX", 1);

    REQUIRE_THROWS_AS(map.add("TX", 99), std::invalid_argument);
    REQUIRE(map.get("TX") == 1); // the original value must be untouched
}

TEST_CASE("update and get reject unknown registers", "[registermap]") {
    RegisterMap<uint32_t> map = makeMap();

    REQUIRE_THROWS_AS(map.update("NOPE", 1), std::out_of_range);
    REQUIRE_THROWS_AS(map.get("NOPE"), std::out_of_range);
}

TEST_CASE("contains reports membership", "[registermap]") {
    RegisterMap<uint32_t> map = makeMap();

    REQUIRE(map.contains("TX"));
    REQUIRE_FALSE(map.contains("NOPE"));
    REQUIRE(map.size() == 3);
}

TEST_CASE("remove is idempotent", "[registermap]") {
    RegisterMap<uint32_t> map = makeMap();

    map.remove("TX");
    REQUIRE_FALSE(map.contains("TX"));

    map.remove("TX"); // removing twice must not throw
    REQUIRE(map.size() == 2);
}

TEST_CASE("keys lists every register name", "[registermap]") {
    RegisterMap<uint32_t> map = makeMap();
    std::vector<std::string> keys = map.keys();

    REQUIRE(keys.size() == 3);
    REQUIRE(contains(keys, "TX"));
    REQUIRE(contains(keys, "MTU"));
}

TEST_CASE("diff reports changed and missing registers", "[registermap]") {
    RegisterMap<uint32_t> before = makeMap();
    RegisterMap<uint32_t> after = makeMap();
    after.update("TX", 42);
    after.remove("MTU");

    std::vector<std::string> changed = before.diff(after);

    REQUIRE(changed.size() == 2);
    REQUIRE(contains(changed, "TX"));
    REQUIRE(contains(changed, "MTU"));
    REQUIRE_FALSE(contains(changed, "RX"));
}

TEST_CASE("diff is asymmetric by design", "[registermap]") {
    RegisterMap<uint32_t> small;
    small.add("TX", 1);

    RegisterMap<uint32_t> big = makeMap();

    // Registers that exist only in the argument are not reported.
    REQUIRE(small.diff(big).empty());
    REQUIRE(big.diff(small).size() == 2);
}

TEST_CASE("equality requires same size and same values", "[registermap]") {
    RegisterMap<uint32_t> a = makeMap();
    RegisterMap<uint32_t> b = makeMap();
    REQUIRE(a == b);

    b.update("TX", 42);
    REQUIRE_FALSE(a == b);

    b.update("TX", 1);
    b.add("EXTRA", 0);
    REQUIRE_FALSE(a == b);
}

// Regression guard: declaring a destructor (even '= default') silently suppresses the implicit
// move operations, turning every snapshot into a full copy.
TEST_CASE("move does not degrade into a copy", "[registermap]") {
    RegisterMap<uint32_t> source = makeMap();

    RegisterMap<uint32_t> moved(std::move(source));

    REQUIRE(moved.size() == 3);
    REQUIRE(source.size() == 0); // a copy would have left the source populated
}
