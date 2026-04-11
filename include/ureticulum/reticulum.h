#pragma once

/* Phase 3 stub. The real Reticulum coordinator class lands in Phase 4.
 * For now we expose only what Identity / Destination / Packet need at
 * compile time. */

namespace RNS {

    class Reticulum {
    public:
        static bool should_use_implicit_proof()  { return _use_implicit_proof; }
        static void should_use_implicit_proof(bool v) { _use_implicit_proof = v; }

        static bool transport_enabled()          { return _transport_enabled; }
        static void transport_enabled(bool v)    { _transport_enabled = v; }

    private:
        static bool _use_implicit_proof;
        static bool _transport_enabled;
    };

}
