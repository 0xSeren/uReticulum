#pragma once

#include <functional>
#include <memory>

#include "ureticulum/bytes.h"
#include "ureticulum/cryptography/ed25519.h"
#include "ureticulum/cryptography/token.h"
#include "ureticulum/cryptography/x25519.h"
#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/type.h"

namespace RNS {

    class Packet;

    /* Wire format:
     *   LINKREQUEST data : ephemeral_x25519_pub (32)
     *                   || ephemeral_ed25519_pub (32)
     *                   || optional advertised_mtu (3, big-endian, masked
     *                      with Type::Link::MTU_BYTEMASK)
     *   LRPROOF data     : signature (64)
     *                   || responder_ephemeral_x25519_pub (32)
     *                   || optional negotiated_mtu (3)
     *
     * The MTU bytes are a uReticulum extension and may not match upstream
     * Reticulum's eventual MTU-discovery format. They're appended-only, so
     * legacy peers that don't send them still work — the responder treats
     * the absence as "use the default MTU".
     *
     * Out of scope: RTT measurement, keep-alive, watchdog timeouts,
     * ratchets, link teardown negotiation, request/response RPC. */
    class Link : public std::enable_shared_from_this<Link> {
    public:
        using Ptr                 = std::shared_ptr<Link>;
        using EstablishedCallback = std::function<void(const Link& link)>;
        using PacketCallback      = std::function<void(const Bytes& plaintext, const Link& link)>;

        enum Status { PENDING, HANDSHAKE, ACTIVE, CLOSED };

        /* Initiator side: build a link request to a known destination and
         * register the link with Transport. The link enters PENDING; it
         * transitions to ACTIVE once an LRPROOF arrives. `advertised_mtu`
         * is the MTU this side wants to use; pass 0 to skip MTU bytes (and
         * fall back to default Reticulum MTU). */
        static Ptr request(const Destination& destination,
                           EstablishedCallback on_established = nullptr,
                           PacketCallback      on_packet      = nullptr,
                           uint32_t            advertised_mtu = 0);

        /* Responder side: validate an incoming LINKREQUEST against a local
         * destination and, if valid, build the link, derive keys, and emit
         * the LRPROOF. Returns the new link or nullptr on failure. */
        static Ptr validate_request(const Destination& owner,
                                    const Bytes&       request_data,
                                    const Packet&      request_packet);

        /* Send an encrypted application payload over the link. */
        void send(const Bytes& plaintext);
        void send_with_context(const Bytes& plaintext, uint8_t context);

        /* Called by Transport when a frame addressed to this link's hash
         * arrives. Decrypts and dispatches to the packet callback. */
        void on_inbound(const Packet& packet);

        /* Called by Transport when an LRPROOF arrives that matches this
         * (initiator-side) link's pending hash. Completes the handshake. */
        void on_proof(const Packet& proof_packet);

        const Bytes&  hash()      const { return _hash; }
        Status        status()    const { return _status; }
        bool          initiator() const { return _initiator; }
        uint32_t      mtu()       const { return _mtu; }

        void set_packet_callback(PacketCallback cb)            { _on_packet = std::move(cb); }
        void set_established_callback(EstablishedCallback cb)  { _on_established = std::move(cb); }

    private:
        Link(const Destination& destination, bool initiator);
        void derive_keys();
        bool open_session_from_peer_pub(const Bytes& peer_x25519_pub);
        void handle_request(const Bytes& plaintext);

        Destination _destination;
        bool        _initiator     = false;
        Status      _status        = PENDING;

        Cryptography::X25519PrivateKey::Ptr  _eph_x25519_prv;
        Cryptography::Ed25519PrivateKey::Ptr _eph_ed25519_prv;
        Bytes _eph_x25519_pub;
        Bytes _eph_ed25519_pub;

        Bytes _peer_x25519_pub;
        Bytes _shared_session_key;
        Bytes _hash;          /* link_id, used as destination hash on the wire */

        uint32_t _local_mtu = 0;  /* what this side advertised; 0 == default */
        uint32_t _mtu       = Type::Reticulum::MTU;

        std::shared_ptr<Cryptography::Token> _token;

        EstablishedCallback _on_established;
        PacketCallback      _on_packet;
    };

}
