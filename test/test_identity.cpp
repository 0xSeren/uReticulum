#include "doctest.h"

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"

using namespace RNS;

TEST_CASE("Identity::createKeys yields valid X25519+Ed25519 pair") {
    Identity id;
    CHECK(id.encryptionPrivateKey().size() == 32);
    CHECK(id.signingPrivateKey().size() == 32);
    CHECK(id.encryptionPublicKey().size() == 32);
    CHECK(id.signingPublicKey().size() == 32);
    CHECK(id.hash().size() == 16);
}

TEST_CASE("Identity sign + validate round-trip") {
    Identity id;
    Bytes msg("hello reticulum");
    Bytes sig = id.sign(msg);
    CHECK(sig.size() == 64);
    CHECK(id.validate(sig, msg));

    Bytes tampered = msg;
    tampered[0] ^= 0x01;
    CHECK_FALSE(id.validate(sig, tampered));
}

TEST_CASE("Identity encrypt + decrypt round-trip") {
    Identity id;
    Bytes plaintext("the quick brown fox");
    Bytes ciphertext = id.encrypt(plaintext);
    CHECK(ciphertext.size() > plaintext.size());
    Bytes decrypted = id.decrypt(ciphertext);
    CHECK(decrypted == plaintext);
}

TEST_CASE("Identity::recall round-trip via remember()") {
    Identity peer;
    Bytes dest_hash = Identity::truncated_hash(peer.get_public_key());

    Identity::remember(/*packet_hash*/Bytes("phash"),
                       dest_hash,
                       peer.get_public_key(),
                       /*app_data*/Bytes("hello"));

    Identity recalled = Identity::recall(dest_hash);
    CHECK((bool)recalled);
    CHECK(recalled.encryptionPublicKey() == peer.encryptionPublicKey());
    CHECK(recalled.signingPublicKey() == peer.signingPublicKey());
    CHECK(recalled.app_data() == Bytes("hello"));
}

TEST_CASE("Destination hash is deterministic") {
    Identity id_a;
    Bytes h1 = Destination::hash(id_a, "myapp", "test");
    Bytes h2 = Destination::hash(id_a, "myapp", "test");
    CHECK(h1 == h2);
    CHECK(h1.size() == 16);

    Identity id_b;
    Bytes h3 = Destination::hash(id_b, "myapp", "test");
    CHECK(h1 != h3);
}

TEST_CASE("Destination encrypt/decrypt SINGLE round-trip") {
    Identity id;
    Destination dest(id, Type::Destination::IN, Type::Destination::SINGLE, "app", "aspect");
    Bytes pt("hello via destination");
    Bytes ct = dest.encrypt(pt);
    CHECK(dest.decrypt(ct) == pt);
}
