#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>

#include "ureticulum/bytes.h"
#include "ureticulum/destination.h"
#include "ureticulum/interface.h"

namespace RNS {

    class Identity;
    class Packet;

    /* Phase 4 Transport.
     *
     * Implements:
     *   - Interface registration
     *   - Local destination registration
     *   - Announce broadcast across interfaces
     *   - Announce validation (Ed25519 signature + destination-hash check)
     *   - Path table updates from received announces
     *   - inbound() routing: locally-addressed → destination callback,
     *     remote-addressed with known next hop → forward, otherwise drop
     *
     * Deferred to later phases:
     *   - Link request handling, link state, link MTU discovery (Phase 5)
     *   - Path requests (PATH_REQUEST), path expiry, rate limiting (Phase 7)
     *   - Resource caching, packet caches, reverse path table
     *   - IFAC, tunnels, probes, persistence
     */
    class Transport {
    public:
        struct PathEntry {
            Bytes      next_hop;
            uint8_t    hops             = 0;
            double     timestamp        = 0;
            std::shared_ptr<InterfaceImpl> via_interface;
        };

        using AnnounceCallback = std::function<void(const Bytes& destination_hash,
                                                    const Identity& announced_identity,
                                                    const Bytes& app_data)>;

        static void register_interface(std::shared_ptr<InterfaceImpl> iface);
        static void deregister_interface(const std::shared_ptr<InterfaceImpl>& iface);
        static const std::vector<std::shared_ptr<InterfaceImpl>>& interfaces();

        static void        register_destination(const Destination& dest);
        static void        deregister_destination(const Destination& dest);
        static Destination find_destination_from_hash(const Bytes& hash);

        /* Send raw bytes on every registered interface, optionally skipping one. */
        static void broadcast(const Bytes& raw, const std::shared_ptr<InterfaceImpl>& skip = nullptr);

        /* Called by Interface::handle_incoming on every received frame. */
        static void inbound(const Bytes& raw, const Interface& iface);

        /* Path table query. */
        static bool          has_path(const Bytes& destination_hash);
        static uint8_t       hops_to(const Bytes& destination_hash);
        static const PathEntry* lookup_path(const Bytes& destination_hash);
        static void          clear_paths();

        /* Optional global announce hook (used by tests and AnnounceHandlers). */
        static void on_announce(AnnounceCallback cb);

        /* Reset everything. Used by tests. */
        static void reset();

    private:
        static std::vector<std::shared_ptr<InterfaceImpl>>      _interfaces;
        static std::map<Bytes, Destination>                     _destinations;
        static std::map<Bytes, PathEntry>                       _path_table;
        static std::set<Bytes>                                  _seen_announces;
        static AnnounceCallback                                 _on_announce;

        static bool process_announce(const Packet& packet, const Interface& iface);
    };

}
