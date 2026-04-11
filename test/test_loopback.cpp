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

TEST_CASE("Loopback exchange of an encrypted DATA packet via Transport") {
    Transport::reset();

    Identity receiver_identity;
    Destination receiver(receiver_identity, Type::Destination::IN,
                         Type::Destination::SINGLE, "test", "loopback");

    std::atomic<int> received{0};
    Bytes last_plaintext;
    receiver.set_packet_callback([&](const Bytes& plaintext, const Packet&) {
        last_plaintext = plaintext;
        received++;
    });
    Transport::register_destination(receiver);

    auto a = LoopbackInterface::create("a");
    auto b = LoopbackInterface::create("b");
    LoopbackInterface::pair(a, b);
    Transport::register_interface(b);

    Identity sender_view_of_receiver(false);
    sender_view_of_receiver.load_public_key(receiver_identity.get_public_key());
    Destination sender_dest(sender_view_of_receiver, Type::Destination::OUT,
                            Type::Destination::SINGLE, receiver.hash());

    Bytes plaintext("hello over loopback");
    Packet packet(sender_dest, plaintext);
    packet.pack();
    a->send_outgoing(packet.raw());

    CHECK(received.load() == 1);
    CHECK(last_plaintext == plaintext);

    Transport::reset();
}

TEST_CASE("Loopback frame round-trips through pack and unpack") {
    Transport::reset();
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
    Transport::reset();
}
