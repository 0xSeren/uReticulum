#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "ureticulum/bytes.h"
#include "ureticulum/destination.h"
#include "ureticulum/interface.h"

namespace RNS {

    class Identity;
    class Link;
    class Packet;

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
        using LinkRequestCallback = std::function<std::shared_ptr<Link>(
            const Destination& destination, const Bytes& request_data, const Packet& packet)>;

        static void register_interface(std::shared_ptr<InterfaceImpl> iface);
        static void deregister_interface(const std::shared_ptr<InterfaceImpl>& iface);
        static const std::vector<std::shared_ptr<InterfaceImpl>>& interfaces();

        static void        register_destination(const Destination& dest);
        static void        deregister_destination(const Destination& dest);
        static Destination find_destination_from_hash(const Bytes& hash);

        /* Link tracking. Phase 5 dispatches inbound frames addressed to a
         * link's hash (LRPROOF and DATA-over-Link) to the registered link. */
        static void                  register_link(const std::shared_ptr<Link>& link);
        static void                  deregister_link(const std::shared_ptr<Link>& link);
        static std::shared_ptr<Link> find_link(const Bytes& link_hash);

        /* Server-side hook: invoked when a LINKREQUEST arrives addressed to a
         * locally-registered destination. The handler should call
         * Link::validate_request and return the resulting link. */
        static void set_link_request_handler(LinkRequestCallback cb);

        static void broadcast(const Bytes& raw, const std::shared_ptr<InterfaceImpl>& skip = nullptr);
        static void inbound(const Bytes& raw, const Interface& iface);

        static bool          has_path(const Bytes& destination_hash);
        static uint8_t       hops_to(const Bytes& destination_hash);
        static const PathEntry* lookup_path(const Bytes& destination_hash);
        static void          clear_paths();

        static void on_announce(AnnounceCallback cb);
        static void reset();

    private:
        /* Recursive because the inbound→process_announce→broadcast→loopback
         * peer→handle_incoming→Transport::inbound chain re-enters on the
         * same thread when running with the in-process LoopbackInterface.
         * Real (out-of-process) interfaces don't need recursion, but the
         * cost is negligible and it lets host tests use loopback freely. */
        static std::recursive_mutex                             _mutex;

        static std::vector<std::shared_ptr<InterfaceImpl>>      _interfaces;
        static std::map<Bytes, Destination>                     _destinations;
        static std::map<Bytes, PathEntry>                       _path_table;
        static std::map<Bytes, std::shared_ptr<Link>>           _links;
        static std::set<Bytes>                                  _seen_announces;
        static AnnounceCallback                                 _on_announce;
        static LinkRequestCallback                              _on_link_request;

        static bool process_announce(const Packet& packet, const Interface& iface);
    };

}
