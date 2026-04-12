#include "doctest.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/interfaces/loopback.h"
#include "ureticulum/transport.h"

using namespace RNS;
using Interfaces::LoopbackInterface;

/* Phase 7+ thread-safety verification. Spawn N producer threads that each
 * inject valid announces into Transport::inbound concurrently, plus M
 * reader threads that hammer has_path / hops_to. Without locking, these
 * would race on _path_table / _seen_announces / _interfaces. With locking,
 * the test should pass cleanly under valgrind/tsan and never corrupt the
 * path table count. */
TEST_CASE("Transport tolerates concurrent inbound from multiple threads") {
    Transport::reset();

    constexpr int N_DESTINATIONS = 16;
    constexpr int N_THREADS      = 4;
    constexpr int ITERATIONS_PER_THREAD = 50;

    /* Pre-build the announce frames so the producer threads don't all hit
     * the random/Ed25519 paths at the same time and inflate the test. */
    std::vector<Bytes>       announces;
    std::vector<Destination> destinations;
    for (int i = 0; i < N_DESTINATIONS; ++i) {
        Identity id;
        Destination dest(id, Type::Destination::IN, Type::Destination::SINGLE,
                         "test", "concurrent");
        destinations.push_back(dest);
        announces.push_back(dest.announce(Bytes("payload"), /*send=*/false));
    }

    auto rx = LoopbackInterface::create("rx");
    Transport::register_interface(rx);
    Interface iface(rx);

    std::atomic<bool> stop_readers{false};
    std::vector<std::thread> threads;

    /* Producers — inject announces concurrently. */
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < ITERATIONS_PER_THREAD; ++i) {
                int idx = (t * ITERATIONS_PER_THREAD + i) % N_DESTINATIONS;
                Transport::inbound(announces[idx], iface);
            }
        });
    }

    /* Readers — hammer the lookup API. They don't check correctness, just
     * survival. If the locking is wrong they'll segfault on a torn map. */
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            while (!stop_readers.load()) {
                for (auto& d : destinations) {
                    (void)Transport::has_path(d.hash());
                    (void)Transport::hops_to(d.hash());
                }
            }
        });
    }

    for (int t = 0; t < N_THREADS; ++t) threads[t].join();
    stop_readers.store(true);
    for (size_t t = N_THREADS; t < threads.size(); ++t) threads[t].join();

    /* All N_DESTINATIONS announces are unique (different identities → different
     * destination_hash), so each must end up in the path table exactly once.
     * The dedup set in Transport guarantees no duplicates regardless of how
     * many times each announce was injected. */
    for (auto& d : destinations) {
        CHECK(Transport::has_path(d.hash()));
    }

    Transport::reset();
}

TEST_CASE("Transport register/deregister concurrency") {
    Transport::reset();

    /* Spam register_destination + deregister_destination from multiple
     * threads with overlapping destination IDs. With proper locking we
     * just want this to not crash and not double-free. */
    constexpr int N_THREADS = 4;
    constexpr int ITERATIONS = 100;

    std::vector<Identity> ids(N_THREADS);
    std::vector<std::thread> threads;
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            Destination d(ids[t], Type::Destination::IN,
                          Type::Destination::SINGLE, "test", "thrash");
            for (int i = 0; i < ITERATIONS; ++i) {
                Transport::register_destination(d);
                Transport::deregister_destination(d);
            }
        });
    }
    for (auto& th : threads) th.join();

    /* After all the register/deregister churn, the table should be empty. */
    for (auto& id : ids) {
        Destination d(id, Type::Destination::IN,
                      Type::Destination::SINGLE, "test", "thrash");
        Destination found = Transport::find_destination_from_hash(d.hash());
        CHECK_FALSE(static_cast<bool>(found));
    }

    Transport::reset();
}
