#include "ureticulum/destination.h"

#include <stdexcept>
#include <string.h>

#include "ureticulum/cryptography/random.h"
#include "ureticulum/packet.h"
#include "ureticulum/transport.h"

namespace RNS {

namespace TD = Type::Destination;

Destination::Destination(const Identity& identity, TD::directions direction, TD::types type,
                         const char* app_name, const char* aspects)
    : _object(std::make_shared<Object>(identity)) {
    if (strchr(app_name, '.') != nullptr)
        throw std::invalid_argument("Dots can't be used in app names");

    _object->_type      = type;
    _object->_direction = direction;

    std::string fullaspects(aspects ? aspects : "");
    if (!identity && direction == TD::IN && type != TD::PLAIN) {
        _object->_identity = Identity();
        fullaspects += "." + _object->_identity.hexhash();
    }
    if (_object->_identity && type == TD::PLAIN)
        throw std::invalid_argument("PLAIN destination cannot hold an identity");

    _object->_name      = expand_name(_object->_identity, app_name, fullaspects.c_str());
    _object->_hash      = hash(_object->_identity, app_name, fullaspects.c_str());
    _object->_hexhash   = _object->_hash.toHex();
    _object->_name_hash = name_hash(app_name, aspects);
}

Destination::Destination(const Identity& identity, TD::directions direction, TD::types type,
                         const Bytes& precomputed_hash)
    : _object(std::make_shared<Object>(identity)) {
    _object->_type      = type;
    _object->_direction = direction;
    if (_object->_identity && type == TD::PLAIN)
        throw std::invalid_argument("PLAIN destination cannot hold an identity");
    _object->_hash      = precomputed_hash;
    _object->_hexhash   = _object->_hash.toHex();
    _object->_name_hash = name_hash("unknown", "unknown");
}

std::string Destination::expand_name(const Identity& identity, const char* app_name, const char* aspects) {
    if (strchr(app_name, '.') != nullptr)
        throw std::invalid_argument("Dots can't be used in app names");
    std::string name(app_name);
    if (aspects && aspects[0]) name += std::string(".") + aspects;
    if (identity)              name += "." + identity.hexhash();
    return name;
}

Bytes Destination::hash(const Identity& identity, const char* app_name, const char* aspects) {
    Bytes addr_material = name_hash(app_name, aspects);
    if (identity) addr_material << identity.hash();
    return Identity::truncated_hash(addr_material);
}

Bytes Destination::name_hash(const char* app_name, const char* aspects) {
    return Identity::full_hash(expand_name(Identity{Type::NONE}, app_name, aspects))
        .left(Type::Identity::NAME_HASH_LENGTH / 8);
}

Bytes Destination::encrypt(const Bytes& data) const {
    if (!_object) return Bytes{Bytes::NONE};
    if (_object->_type == TD::PLAIN) return data;
    if (_object->_type == TD::SINGLE && _object->_identity)
        return _object->_identity.encrypt(data);
    return data;
}

Bytes Destination::decrypt(const Bytes& data) const {
    if (!_object) return Bytes{Bytes::NONE};
    if (_object->_type == TD::PLAIN) return data;
    if (_object->_type == TD::SINGLE && _object->_identity)
        return _object->_identity.decrypt(data);
    return Bytes{Bytes::NONE};
}

Bytes Destination::sign(const Bytes& message) const {
    if (_object && _object->_type == TD::SINGLE && _object->_identity)
        return _object->_identity.sign(message);
    return Bytes{Bytes::NONE};
}

void Destination::receive(const Bytes& plaintext, const Packet& packet) const {
    if (_object && _object->_packet_callback) _object->_packet_callback(plaintext, packet);
}

Bytes Destination::announce(const Bytes& app_data, bool send) const {
    if (!_object || _object->_type != TD::SINGLE || _object->_direction != TD::IN)
        throw std::invalid_argument("Only SINGLE+IN destinations can be announced");

    Bytes random_hash = Cryptography::random(Type::Identity::RANDOM_HASH_LENGTH / 8);

    Bytes signed_data;
    signed_data << _object->_hash
                << _object->_identity.get_public_key()
                << _object->_name_hash
                << random_hash;
    if (!app_data.empty()) signed_data << app_data;

    Bytes signature = _object->_identity.sign(signed_data);

    Bytes announce_data;
    announce_data << _object->_identity.get_public_key()
                  << _object->_name_hash
                  << random_hash
                  << signature;
    if (!app_data.empty()) announce_data << app_data;

    Packet announce_packet(*this, announce_data, Type::Packet::ANNOUNCE);
    announce_packet.pack();
    Bytes raw = announce_packet.raw();
    if (send) Transport::broadcast(raw);
    return raw;
}

}
