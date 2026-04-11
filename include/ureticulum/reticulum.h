#pragma once

#include <atomic>

#include "ureticulum/hal.h"

namespace RNS {

    /* Reticulum coordinator. Phase 7 wraps Transport's job loop in a HAL
     * task so firmware can run uReticulum as a self-contained background
     * service. The Phase 3 stub flags (should_use_implicit_proof,
     * transport_enabled) live here too — they were always meant to.
     *
     * Thread-safety note: Transport's static state is currently not
     * mutex-protected. Phase 7 only puts the loop on its own task; it does
     * not yet harden against parallel inbound dispatch from multiple
     * interface tasks. That hardening is a follow-up that requires deciding
     * the lock granularity (single mutex / recursive / per-section). */
    class Reticulum {
    public:
        static bool should_use_implicit_proof()        { return _use_implicit_proof; }
        static void should_use_implicit_proof(bool v)  { _use_implicit_proof = v; }
        static bool transport_enabled()                { return _transport_enabled; }
        static void transport_enabled(bool v)          { _transport_enabled = v; }

        /* Spawn the Reticulum loop on a HAL task. tick_ms is the interval
         * between loop iterations. Returns true on success. */
        static bool start(uint32_t tick_ms = 100,
                          size_t   stack_words = 4096,
                          int      priority    = 5);

        /* Signal the loop task to exit. Idempotent. The task will finish its
         * current iteration before exiting. */
        static void stop();

        static bool is_running() { return _running.load(); }

        /* Single iteration of the Reticulum loop. Public so the caller can
         * also drive it cooperatively from their own loop instead of
         * letting Reticulum spawn a task. */
        static void run_once();

    private:
        static std::atomic<bool> _running;
        static std::atomic<bool> _stop_requested;
        static uint32_t          _tick_ms;
        static bool              _use_implicit_proof;
        static bool              _transport_enabled;

        static void task_entry(void* arg);
    };

}
