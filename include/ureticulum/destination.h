#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ureticulum/bytes.h"
#include "ureticulum/identity.h"
#include "ureticulum/type.h"

namespace RNS {

    class Packet;

    /* Phase 3 cut: SINGLE / IN+OUT only. No Link tracking, no announce, no
     * request handlers. Phase 4-5 add the rest. */
    class Destination {
    public:
        using PacketCallback = std::function<void(const Bytes& data, const Packet& packet)>;

        Destination(Type::NoneConstructor) {}
        Destination(const Destination& other) : _object(other._object) {}
        Destination(const Identity& identity,
                    Type::Destination::directions direction,
                    Type::Destination::types       type,
                    const char* app_name,
                    const char* aspects);
        Destination(const Identity& identity,
                    Type::Destination::directions direction,
                    Type::Destination::types       type,
                    const Bytes& precomputed_hash);
        virtual ~Destination() = default;

        Destination& operator=(const Destination& other) { _object = other._object; return *this; }
        explicit operator bool() const { return _object != nullptr; }
        bool operator<(const Destination& other) const { return _object.get() < other._object.get(); }

        static std::string expand_name(const Identity& identity, const char* app_name, const char* aspects);
        static Bytes hash(const Identity& identity, const char* app_name, const char* aspects);
        static Bytes name_hash(const char* app_name, const char* aspects);

        void set_packet_callback(PacketCallback cb) { _object->_packet_callback = std::move(cb); }

        Bytes encrypt(const Bytes& data) const;
        Bytes decrypt(const Bytes& data) const;
        Bytes sign(const Bytes& message) const;

        /* Invoked by the loopback path when a packet matching this destination
         * arrives. Phase 4 Transport calls this from its routing engine. */
        void receive(const Bytes& plaintext, const Packet& packet) const;

        Type::Destination::types      type()      const { return _object->_type; }
        Type::Destination::directions direction() const { return _object->_direction; }
        const Bytes&                  hash()      const { return _object->_hash; }
        const Identity&               identity()  const { return _object->_identity; }

        std::string toString() const { return _object ? "{Destination:" + _object->_hash.toHex() + "}" : ""; }

    private:
        struct Object {
            Object(const Identity& id) : _identity(id) {}
            Type::Destination::types      _type;
            Type::Destination::directions _direction;
            Identity                      _identity;
            std::string                   _name;
            Bytes                         _hash;
            Bytes                         _name_hash;
            std::string                   _hexhash;
            PacketCallback                _packet_callback;
        };
        std::shared_ptr<Object> _object;
    };

}
