#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ureticulum/bytes.h"
#include "ureticulum/identity.h"
#include "ureticulum/type.h"

namespace RNS {

    class Packet;

    class Destination {
    public:
        using PacketCallback = std::function<void(const Bytes& data, const Packet& packet)>;

        /* Request handler: called when a peer sends a Link request to a
         * registered path. Returns response bytes (or empty to send no
         * response). Signature matches Python Reticulum's response_generator. */
        using ResponseGenerator = std::function<Bytes(
            const Bytes& path, const Bytes& data, const Bytes& request_id,
            const Bytes& link_id, const Identity& remote_identity, double requested_at)>;

        struct RequestHandler {
            Bytes path;
            Bytes path_hash;
            ResponseGenerator generator;
        };

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

        /* Register a request handler for a named path (e.g. "/page/index.mu").
         * When a peer sends a Link REQUEST with the corresponding path_hash,
         * the generator is called and the response is sent back. */
        void register_request_handler(const char* path, ResponseGenerator gen);
        const std::map<Bytes, RequestHandler>& request_handlers() const { return _object->_request_handlers; }

        Bytes encrypt(const Bytes& data) const;
        Bytes decrypt(const Bytes& data) const;
        Bytes sign(const Bytes& message) const;

        /* Build (and optionally broadcast) an announce packet for this
         * destination. Returns the packed raw frame. SINGLE / IN only. */
        Bytes announce(const Bytes& app_data = Bytes{Bytes::NONE}, bool send = true) const;

        /* Invoked by Transport when a packet matching this destination arrives. */
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
            std::map<Bytes, RequestHandler> _request_handlers;
        };
        std::shared_ptr<Object> _object;
    };

}
