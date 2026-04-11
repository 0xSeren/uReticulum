#include "doctest.h"

#include <atomic>

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/interface.h"
#include "ureticulum/interfaces/loopback.h"
#include "ureticulum/packet.h"
#include "ureticulum/transport.h"

using namespace RNS;
using Interfaces::LoopbackInterface;

namespace {
    /* The Phase 3 loopback rig: a single global "router" callback, set into
     * the Transport stub, that decodes incoming frames and dispatches them to
     * a registered destination by hash. Phase 4's real Transport replaces all
     * of this. */
    struct Rig {
        Destination receive_dest = Destination{Type::NONE};
        std::atomic<int> packets_received{0};
        Bytes last_plaintext;
    };
    Rig g_rig;

    void on_inbound(const Bytes& data, const Interface& iface) {
        (void)iface;
        Packet pkt(data);
        if (!pkt.unpack()) return;
        if (!g_rig.receive_dest) return;
        if (pkt.destination_hash() != g_rig.receive_dest.hash()) return;
        Bytes plaintext = g_rig.receive_dest.decrypt(pkt.data());
        if (plaintext.empty()) return;
        g_rig.last_plaintext = plaintext;
        g_rig.packets_received++;
    }
}

TEST_CASE("Loopback exchange of an encrypted DATA packet") {
    /* Reset rig state in case the test runs multiple times. */
    g_rig.receive_dest = Destination{Type::NONE};
    g_rig.packets_received.store(0);
    g_rig.last_plaintext.clear();

    Transport::set_inbound_callback(&on_inbound);

    /* Receiver identity + destination. */
    Identity receiver_identity;
    Destination receiver(receiver_identity, Type::Destination::IN,
                         Type::Destination::SINGLE, "test", "loopback");
    g_rig.receive_dest = receiver;

    /* Sender side has only the receiver's public key — no private key. */
    Identity sender_view_of_receiver(false);
    sender_view_of_receiver.load_public_key(receiver_identity.get_public_key());
    Destination sender_dest(sender_view_of_receiver, Type::Destination::OUT,
                            Type::Destination::SINGLE, receiver.hash());

    /* Wire up the loopback. */
    auto a = LoopbackInterface::create("a");
    auto b = LoopbackInterface::create("b");
    LoopbackInterface::pair(a, b);

    /* Build and send a DATA packet on interface A. The frame arrives on B
     * via the pair link, B's handle_incoming pumps it through Transport
     * which invokes our on_inbound callback. */
    Bytes plaintext("hello over loopback");
    Packet packet(sender_dest, plaintext);
    packet.pack();
    a->send_outgoing(packet.raw());

    CHECK(g_rig.packets_received.load() == 1);
    CHECK(g_rig.last_plaintext == plaintext);

    Transport::set_inbound_callback(nullptr);
    g_rig.receive_dest = Destination{Type::NONE};
    g_rig.packets_received.store(0);
    g_rig.last_plaintext.clear();
}

TEST_CASE("Loopback frame survives encode + decode round-trip") {
    /* Pure pack/unpack — no crypto, no Transport involved. Verifies the
     * Phase 3 wire frame round-trips bit-for-bit. */
    Identity id;
    Destination dest(id, Type::Destination::IN, Type::Destination::SINGLE, "test", "frame");
    Packet outgoing(dest, Bytes("payload"));
    outgoing.pack();

    Packet incoming(outgoing.raw());
    REQUIRE(incoming.unpack());
    CHECK(incoming.destination_hash() == dest.hash());
    CHECK(incoming.packet_type() == Type::Packet::DATA);
    CHECK(incoming.context() == Type::Packet::CONTEXT_NONE);
    CHECK(incoming.header_type() == Type::Packet::HEADER_1);
}
