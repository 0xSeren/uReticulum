#pragma once

#include <map>
#include <memory>
#include <string>

#include "ureticulum/bytes.h"
#include "ureticulum/cryptography/ed25519.h"
#include "ureticulum/cryptography/hashes.h"
#include "ureticulum/cryptography/random.h"
#include "ureticulum/cryptography/x25519.h"
#include "ureticulum/log.h"
#include "ureticulum/type.h"

namespace RNS {

    class Destination;
    class Packet;

    class Identity {
    private:
        struct IdentityEntry {
            IdentityEntry() = default;
            IdentityEntry(double timestamp, const Bytes& packet_hash, const Bytes& public_key, const Bytes& app_data)
                : _timestamp(timestamp), _packet_hash(packet_hash), _public_key(public_key), _app_data(app_data) {}
            double _timestamp = 0;
            Bytes  _packet_hash;
            Bytes  _public_key;
            Bytes  _app_data;
        };
        using IdentityTable = std::map<Bytes, IdentityEntry>;

        static IdentityTable _known_destinations;
        static uint16_t      _known_destinations_maxsize;

    public:
        Identity(bool create_keys = true);
        Identity(Type::NoneConstructor) {}
        Identity(const Identity& other) : _object(other._object) {}
        virtual ~Identity() = default;

        Identity& operator=(const Identity& other) { _object = other._object; return *this; }
        explicit operator bool() const { return _object != nullptr; }
        bool operator<(const Identity& other) const { return _object.get() < other._object.get(); }

        void createKeys();

        Bytes get_private_key() const { return _object->_prv_bytes + _object->_sig_prv_bytes; }
        Bytes get_public_key()  const { return _object->_pub_bytes + _object->_sig_pub_bytes; }

        bool load_private_key(const Bytes& prv_bytes);
        void load_public_key(const Bytes& pub_bytes);
        void update_hashes();

        const Bytes& get_salt()    const { return _object->_hash; }
        Bytes        get_context() const { return Bytes{Bytes::NONE}; }

        Bytes encrypt(const Bytes& plaintext) const;
        Bytes decrypt(const Bytes& ciphertext_token) const;
        Bytes sign(const Bytes& message) const;
        bool  validate(const Bytes& signature, const Bytes& message) const;

        static void    remember(const Bytes& packet_hash, const Bytes& destination_hash,
                                const Bytes& public_key, const Bytes& app_data = Bytes{Bytes::NONE});
        static Identity recall(const Bytes& destination_hash);
        static Bytes    recall_app_data(const Bytes& destination_hash);

        static Bytes full_hash(const Bytes& data) { return Cryptography::sha256(data); }
        static Bytes truncated_hash(const Bytes& data) {
            return full_hash(data).left(Type::Identity::TRUNCATED_HASHLENGTH / 8);
        }
        static Bytes get_random_hash() {
            return truncated_hash(Cryptography::random(Type::Identity::TRUNCATED_HASHLENGTH / 8));
        }

        const Bytes&  encryptionPrivateKey() const { return _object->_prv_bytes; }
        const Bytes&  signingPrivateKey()    const { return _object->_sig_prv_bytes; }
        const Bytes&  encryptionPublicKey()  const { return _object->_pub_bytes; }
        const Bytes&  signingPublicKey()     const { return _object->_sig_pub_bytes; }
        const Bytes&  hash()                 const { return _object->_hash; }
        std::string   hexhash()              const { return _object->_hexhash; }
        const Bytes&  app_data()             const { return _object->_app_data; }
        void          app_data(const Bytes& d)     { _object->_app_data = d; }

        Cryptography::X25519PrivateKey::Ptr  prv()      const { return _object->_prv; }
        Cryptography::Ed25519PrivateKey::Ptr sig_prv()  const { return _object->_sig_prv; }
        Cryptography::X25519PublicKey::Ptr   pub()      const { return _object->_pub; }
        Cryptography::Ed25519PublicKey::Ptr  sig_pub()  const { return _object->_sig_pub; }

        std::string toString() const { return _object ? "{Identity:" + _object->_hash.toHex() + "}" : ""; }

    private:
        struct Object {
            Cryptography::X25519PrivateKey::Ptr  _prv;
            Bytes                                _prv_bytes;
            Cryptography::Ed25519PrivateKey::Ptr _sig_prv;
            Bytes                                _sig_prv_bytes;
            Cryptography::X25519PublicKey::Ptr   _pub;
            Bytes                                _pub_bytes;
            Cryptography::Ed25519PublicKey::Ptr  _sig_pub;
            Bytes                                _sig_pub_bytes;
            Bytes                                _hash;
            std::string                          _hexhash;
            Bytes                                _app_data;
        };
        std::shared_ptr<Object> _object;

        friend class Transport;
    };

}
