#pragma once

/* Phase 3 stub. Real Transport (path table, announce propagation, packet
 * routing) lands in Phase 4. The stub provides only the static hooks that
 * Identity / Destination / Packet call into so the type-system stays
 * consistent across phases. */

#include "ureticulum/bytes.h"

namespace RNS {

    class Identity;
    class Destination;
    class Interface;

    class Transport {
    public:
        /* Phase-3 callback hook used by interfaces to deliver received frames.
         * Phase 4 replaces this with the real routing engine. */
        using InboundCallback = void(*)(const Bytes& data, const Interface& iface);
        static void set_inbound_callback(InboundCallback cb);
        static void inbound(const Bytes& data, const Interface& iface);

        /* Stubbed lookup. Phase 4 implements with the real path table. */
        static Destination find_destination_from_hash(const Bytes& hash);

    private:
        static InboundCallback _on_inbound;
    };

}
