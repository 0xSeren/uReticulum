#pragma once

#include <memory>

#include "ureticulum/bytes.h"
#include "ureticulum/destination.h"
#include "ureticulum/type.h"

namespace RNS {

    class Interface;

    /* Phase 3 cut: only DATA packets via HEADER_1 + BROADCAST get full
     * encode/decode support. Link / LRPROOF / announce / proof variants
     * land in Phase 4-5 along with the real Transport. */
    class Packet {
    public:
        Packet(Type::NoneConstructor) {}
        Packet(const Packet& other) : _object(other._object) {}
        Packet& operator=(const Packet& other) { _object = other._object; return *this; }
        explicit operator bool() const { return _object != nullptr; }

        /* Outgoing packet (encode side). */
        Packet(const Destination& destination,
               const Bytes&       data,
               Type::Packet::types         packet_type    = Type::Packet::DATA,
               Type::Packet::context_types context        = Type::Packet::CONTEXT_NONE,
               Type::Transport::types      transport_type = Type::Transport::BROADCAST);

        /* Incoming raw frame (decode side). After construction the caller
         * invokes unpack() to populate header fields and ciphertext. */
        explicit Packet(const Bytes& raw_frame);

        void  pack();
        bool  unpack();

        Bytes get_hash() const;
        Bytes get_hashable_part() const;

        Type::Packet::types         packet_type()   const { return _object->_packet_type; }
        Type::Packet::context_types context()       const { return _object->_context; }
        Type::Packet::header_types  header_type()   const { return _object->_header_type; }
        const Bytes&                destination_hash() const { return _object->_destination_hash; }
        const Bytes&                raw()           const { return _object->_raw; }
        const Bytes&                data()          const { return _object->_data; }
        uint8_t                     hops()          const { return _object->_hops; }

    private:
        struct Object {
            Type::Packet::header_types  _header_type    = Type::Packet::HEADER_1;
            Type::Transport::types      _transport_type = Type::Transport::BROADCAST;
            Type::Destination::types    _destination_type = Type::Destination::SINGLE;
            Type::Packet::types         _packet_type    = Type::Packet::DATA;
            Type::Packet::context_types _context        = Type::Packet::CONTEXT_NONE;
            Type::Packet::context_flags _context_flag   = Type::Packet::FLAG_UNSET;
            uint8_t _flags = 0;
            uint8_t _hops  = 0;
            Destination _destination = Destination{Type::NONE};
            Bytes _destination_hash;
            Bytes _data;
            Bytes _raw;
            Bytes _packet_hash;
            bool  _packed = false;
            bool  _from_packed = false;
        };
        std::shared_ptr<Object> _object;

        uint8_t pack_flags() const;
        void    unpack_flags(uint8_t flags);
    };

}
