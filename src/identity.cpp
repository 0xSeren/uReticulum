#include "ureticulum/identity.h"

#include <stdexcept>

#include "ureticulum/cryptography/hkdf.h"
#include "ureticulum/cryptography/token.h"
#include "ureticulum/os.h"

namespace RNS {

#ifndef RNS_KNOWN_DESTINATIONS_MAX
#define RNS_KNOWN_DESTINATIONS_MAX 100
#endif

Identity::IdentityTable Identity::_known_destinations;
uint16_t                Identity::_known_destinations_maxsize = RNS_KNOWN_DESTINATIONS_MAX;

Identity::Identity(bool create_keys) : _object(std::make_shared<Object>()) {
    if (create_keys) createKeys();
}

void Identity::createKeys() {
    _object->_prv           = Cryptography::X25519PrivateKey::generate();
    _object->_prv_bytes     = _object->_prv->private_bytes();
    _object->_sig_prv       = Cryptography::Ed25519PrivateKey::generate();
    _object->_sig_prv_bytes = _object->_sig_prv->private_bytes();
    _object->_pub           = _object->_prv->public_key();
    _object->_pub_bytes     = _object->_pub->public_bytes();
    _object->_sig_pub       = _object->_sig_prv->public_key();
    _object->_sig_pub_bytes = _object->_sig_pub->public_bytes();
    update_hashes();
    VERBOSEF("Identity keys created for %s", _object->_hash.toHex().c_str());
}

bool Identity::load_private_key(const Bytes& prv_bytes) {
    try {
        _object->_prv_bytes     = prv_bytes.left(Type::Identity::KEYSIZE / 8 / 2);
        _object->_prv           = Cryptography::X25519PrivateKey::from_private_bytes(_object->_prv_bytes);
        _object->_sig_prv_bytes = prv_bytes.mid(Type::Identity::KEYSIZE / 8 / 2);
        _object->_sig_prv       = Cryptography::Ed25519PrivateKey::from_private_bytes(_object->_sig_prv_bytes);
        _object->_pub           = _object->_prv->public_key();
        _object->_pub_bytes     = _object->_pub->public_bytes();
        _object->_sig_pub       = _object->_sig_prv->public_key();
        _object->_sig_pub_bytes = _object->_sig_pub->public_bytes();
        update_hashes();
        return true;
    } catch (std::exception& e) {
        ERRORF("load_private_key failed: %s", e.what());
        return false;
    }
}

void Identity::load_public_key(const Bytes& pub_bytes) {
    try {
        _object->_pub_bytes     = pub_bytes.left(Type::Identity::KEYSIZE / 8 / 2);
        _object->_sig_pub_bytes = pub_bytes.mid(Type::Identity::KEYSIZE / 8 / 2);
        _object->_pub           = Cryptography::X25519PublicKey::from_public_bytes(_object->_pub_bytes);
        _object->_sig_pub       = Cryptography::Ed25519PublicKey::from_public_bytes(_object->_sig_pub_bytes);
        update_hashes();
    } catch (std::exception& e) {
        ERRORF("load_public_key failed: %s", e.what());
    }
}

void Identity::update_hashes() {
    _object->_hash    = truncated_hash(get_public_key());
    _object->_hexhash = _object->_hash.toHex();
}

void Identity::remember(const Bytes& packet_hash, const Bytes& destination_hash,
                        const Bytes& public_key, const Bytes& app_data) {
    if (public_key.size() != Type::Identity::KEYSIZE / 8) {
        throw std::invalid_argument("Identity::remember: invalid public key size");
    }
    _known_destinations[destination_hash] = {Utilities::OS::time(), packet_hash, public_key, app_data};
    /* Phase 4 will add LRU culling against _known_destinations_maxsize. */
}

Identity Identity::recall(const Bytes& destination_hash) {
    auto it = _known_destinations.find(destination_hash);
    if (it == _known_destinations.end()) return Identity{Type::NONE};
    Identity id(false);
    id.load_public_key(it->second._public_key);
    id.app_data(it->second._app_data);
    return id;
}

Bytes Identity::recall_app_data(const Bytes& destination_hash) {
    auto it = _known_destinations.find(destination_hash);
    if (it == _known_destinations.end()) return Bytes{Bytes::NONE};
    return it->second._app_data;
}

Bytes Identity::encrypt(const Bytes& plaintext) const {
    if (!_object || !_object->_pub)
        throw std::runtime_error("Encryption failed: no public key");

    auto  ephemeral_key       = Cryptography::X25519PrivateKey::generate();
    Bytes ephemeral_pub_bytes = ephemeral_key->public_key()->public_bytes();
    Bytes shared_key          = ephemeral_key->exchange(_object->_pub_bytes);
    Bytes derived_key         = Cryptography::hkdf(Type::Identity::DERIVED_KEY_LENGTH,
                                                   shared_key, get_salt(), get_context());
    Cryptography::Token token(derived_key);
    return ephemeral_pub_bytes + token.encrypt(plaintext);
}

Bytes Identity::decrypt(const Bytes& ciphertext_token) const {
    if (!_object || !_object->_prv)
        throw std::runtime_error("Decryption failed: no private key");
    if (ciphertext_token.size() <= Type::Identity::KEYSIZE / 8 / 2) {
        DEBUGF("decrypt: token too short (%zu)", ciphertext_token.size());
        return Bytes{Bytes::NONE};
    }
    try {
        Bytes peer_pub_bytes = ciphertext_token.left(Type::Identity::KEYSIZE / 8 / 2);
        Bytes shared_key     = _object->_prv->exchange(peer_pub_bytes);
        Bytes derived_key    = Cryptography::hkdf(Type::Identity::DERIVED_KEY_LENGTH,
                                                  shared_key, get_salt(), get_context());
        Cryptography::Token token(derived_key);
        Bytes ciphertext = ciphertext_token.mid(Type::Identity::KEYSIZE / 8 / 2);
        return token.decrypt(ciphertext);
    } catch (std::exception& e) {
        DEBUGF("Decryption by %s failed: %s", toString().c_str(), e.what());
        return Bytes{Bytes::NONE};
    }
}

Bytes Identity::sign(const Bytes& message) const {
    if (!_object || !_object->_sig_prv)
        throw std::runtime_error("Signing failed: no signing key");
    return _object->_sig_prv->sign(message);
}

bool Identity::validate(const Bytes& signature, const Bytes& message) const {
    if (!_object || !_object->_sig_pub) return false;
    return _object->_sig_pub->verify(signature, message);
}

}
