#include "doctest.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "ureticulum/interface.h"
#include "ureticulum/reticulum.h"
#include "ureticulum/transport.h"

using namespace RNS;

namespace {
    /* A trivial interface that increments a counter every time loop() is
     * called. Used to verify the Reticulum task actually pumps interfaces. */
    struct CounterInterface : public InterfaceImpl {
        std::atomic<int> ticks{0};
        CounterInterface() : InterfaceImpl("counter") {}
        void send_outgoing(const Bytes&) override {}
        void loop() override { ticks++; }
    };
}

TEST_CASE("Reticulum::run_once pumps interface loop hooks") {
    Transport::reset();
    auto iface = std::make_shared<CounterInterface>();
    Transport::register_interface(iface);

    CHECK(iface->ticks.load() == 0);
    Reticulum::run_once();
    CHECK(iface->ticks.load() == 1);
    Reticulum::run_once();
    Reticulum::run_once();
    CHECK(iface->ticks.load() == 3);

    Transport::reset();
}

TEST_CASE("Reticulum task runs in background and stops cleanly") {
    Transport::reset();
    auto iface = std::make_shared<CounterInterface>();
    Transport::register_interface(iface);

    /* Tick fast (10 ms) so the test stays quick. */
    REQUIRE(Reticulum::start(/*tick_ms=*/10));
    CHECK(Reticulum::is_running());

    /* Give it ~80 ms — at 10ms tick that's ~8 iterations. Be lenient. */
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    Reticulum::stop();
    /* Wait for the task to actually exit. */
    for (int i = 0; i < 50 && Reticulum::is_running(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    CHECK_FALSE(Reticulum::is_running());
    CHECK(iface->ticks.load() >= 3);

    /* Calling stop again should be idempotent. */
    Reticulum::stop();
    CHECK_FALSE(Reticulum::is_running());

    Transport::reset();
}

TEST_CASE("Reticulum::start refuses double-start") {
    Transport::reset();
    auto iface = std::make_shared<CounterInterface>();
    Transport::register_interface(iface);

    REQUIRE(Reticulum::start(10));
    CHECK_FALSE(Reticulum::start(10));  /* second start returns false */

    Reticulum::stop();
    for (int i = 0; i < 50 && Reticulum::is_running(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Transport::reset();
}
