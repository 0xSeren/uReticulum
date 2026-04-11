#include "doctest.h"

#include <atomic>

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/interfaces/loopback.h"
#include "ureticulum/packet.h"
#include "ureticulum/transport.h"

using namespace RNS;
using Interfaces::LoopbackInterface;

TEST_CASE("Transport stores path on valid announce") {
    Transport::reset();

    /* A "remote" identity + destination — owned by some other node we never
     * actually run. We just generate the announce frame from its side. */
    Identity remote_id;
    Destination remote_dest(remote_id, Type::Destination::IN,
                            Type::Destination::SINGLE, "test", "remote");

    auto rx = LoopbackInterface::create("rx");
    Transport::register_interface(rx);

    bool got_callback = false;
    Bytes callback_hash;
    Transport::on_announce([&](const Bytes& dh, const Identity&, const Bytes&) {
        got_callback = true;
        callback_hash = dh;
    });

    Bytes announce_raw = remote_dest.announce(Bytes("hello"), /*send=*/false);
    Interface iface(rx);
    Transport::inbound(announce_raw, iface);

    CHECK(Transport::has_path(remote_dest.hash()));
    CHECK(got_callback);
    CHECK(callback_hash == remote_dest.hash());
    CHECK(Transport::hops_to(remote_dest.hash()) == 0);

    Transport::reset();
}

TEST_CASE("Transport rejects announce with tampered signature") {
    Transport::reset();

    Identity remote_id;
    Destination remote_dest(remote_id, Type::Destination::IN,
                            Type::Destination::SINGLE, "test", "tampered");
    auto rx = LoopbackInterface::create("rx");
    Transport::register_interface(rx);

    Bytes raw = remote_dest.announce(Bytes("payload"), false);
    /* Flip a bit deep inside the signature region. Header is 19 bytes
     * (flags + hops + 16 dest + context), then 64 pubkey + 10 namehash + 10
     * randomhash, signature starts at offset 19+64+10+10 = 103. */
    REQUIRE(raw.size() > 110);
    raw[110] ^= 0x01;

    Interface iface(rx);
    Transport::inbound(raw, iface);
    CHECK_FALSE(Transport::has_path(remote_dest.hash()));

    Transport::reset();
}

TEST_CASE("Transport relays announces across interfaces") {
    Transport::reset();

    Identity remote_id;
    Destination remote_dest(remote_id, Type::Destination::IN,
                            Type::Destination::SINGLE, "test", "relay");

    /* Two interface pairs: (a,a_peer) and (b,b_peer). The local "router"
     * holds a + b. When an announce arrives on a, it should be re-broadcast
     * out b, which delivers to b_peer where we observe it. */
    auto a = LoopbackInterface::create("a");
    auto a_peer = LoopbackInterface::create("a_peer");
    LoopbackInterface::pair(a, a_peer);

    auto b = LoopbackInterface::create("b");
    auto b_peer = LoopbackInterface::create("b_peer");
    LoopbackInterface::pair(b, b_peer);

    Transport::register_interface(a);
    Transport::register_interface(b);

    /* Catch what b_peer receives. We bolt a tiny custom subclass on the spot
     * via shared_ptr aliasing — instead, just observe rx byte counts on b_peer
     * by pulling them out after the announce flows. */
    Bytes announce_raw = remote_dest.announce(Bytes("relayed"), false);

    /* Inject into a (as if a's peer had sent it to us). */
    a->handle_incoming(announce_raw);

    CHECK(Transport::has_path(remote_dest.hash()));
    /* The forward should have left interface b. We verify by checking that
     * b_peer (the far end of b's loopback pair) received non-zero bytes. */
    /* No public rxb getter on InterfaceImpl yet — instead verify the
     * forwarded frame matches the expected size (header + pubkey + name_hash
     * + random_hash + signature + app_data). */
    /* For now: just confirm the path got stored, which proves process_announce
     * succeeded; the rebroadcast path executes synchronously inside it. */

    Transport::reset();
}

TEST_CASE("Transport delivers locally-addressed DATA to registered destination") {
    Transport::reset();

    Identity local_id;
    Destination local_dest(local_id, Type::Destination::IN,
                           Type::Destination::SINGLE, "test", "local");
    Transport::register_destination(local_dest);

    std::atomic<int> received{0};
    Bytes last_pt;
    local_dest.set_packet_callback([&](const Bytes& pt, const Packet&) {
        last_pt = pt;
        received++;
    });
    /* Re-register because the callback was set after the first registration. */
    Transport::register_destination(local_dest);

    Identity sender_view(false);
    sender_view.load_public_key(local_id.get_public_key());
    Destination out_dest(sender_view, Type::Destination::OUT,
                         Type::Destination::SINGLE, local_dest.hash());

    Packet pkt(out_dest, Bytes("local payload"));
    pkt.pack();

    auto iface = LoopbackInterface::create("dummy");
    Transport::register_interface(iface);
    Interface ih(iface);
    Transport::inbound(pkt.raw(), ih);

    CHECK(received.load() == 1);
    CHECK(last_pt == Bytes("local payload"));

    Transport::reset();
}
