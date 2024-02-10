#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "minisdk/EventBus.hpp"

using minisdk::EventBus;

TEST_CASE("every subscriber of an event is notified once per publish", "[eventbus]") {
    EventBus bus;
    bus.addEvent("LINK");

    int first = 0;
    int second = 0;
    std::string seen;

    bus.addSubscriber("LINK", [&](const std::string& message) { ++first; seen = message; });
    bus.addSubscriber("LINK", [&](const std::string&) { ++second; });

    bus.eventHappen("LINK", "up");

    REQUIRE(first == 1);
    REQUIRE(second == 1);
    REQUIRE(seen == "up");
}

TEST_CASE("publishing to an event without subscribers is harmless", "[eventbus]") {
    EventBus bus;
    bus.addEvent("LINK");

    REQUIRE_NOTHROW(bus.eventHappen("LINK", "up"));
}

TEST_CASE("publishing an unregistered event is ignored", "[eventbus]") {
    EventBus bus;

    REQUIRE_NOTHROW(bus.eventHappen("NEVER_REGISTERED", "payload"));
}

TEST_CASE("subscribing to an unregistered event is rejected", "[eventbus]") {
    EventBus bus;

    REQUIRE_THROWS_AS(bus.addSubscriber("NOPE", [](const std::string&) {}), std::invalid_argument);
}

TEST_CASE("registering the same event twice is rejected", "[eventbus]") {
    EventBus bus;
    bus.addEvent("LINK");

    REQUIRE_THROWS_AS(bus.addEvent("LINK"), std::invalid_argument);
}

TEST_CASE("removing an event stops delivery", "[eventbus]") {
    EventBus bus;
    bus.addEvent("LINK");

    int calls = 0;
    bus.addSubscriber("LINK", [&](const std::string&) { ++calls; });

    bus.removeEvent("LINK");
    bus.eventHappen("LINK", "up");

    REQUIRE(calls == 0);
}

// Regression guard: subscribers are copied before being invoked and run outside the bus lock.
// Without that, a callback touching the bus either invalidates the iterator or deadlocks.
TEST_CASE("a subscriber may modify the bus from inside its callback", "[eventbus]") {
    EventBus bus;
    bus.addEvent("LINK");

    int calls = 0;
    bus.addSubscriber("LINK", [&](const std::string&) {
        ++calls;
        bus.addEvent("SPAWNED_FROM_CALLBACK");
        bus.addSubscriber("SPAWNED_FROM_CALLBACK", [](const std::string&) {});
    });

    REQUIRE_NOTHROW(bus.eventHappen("LINK", "up"));
    REQUIRE(calls == 1);
}
