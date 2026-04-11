#include "ureticulum/transport.h"

#include <algorithm>

#include "ureticulum/identity.h"
#include "ureticulum/log.h"
#include "ureticulum/os.h"
#include "ureticulum/packet.h"

namespace RNS {

std::vector<std::shared_ptr<InterfaceImpl>> Transport::_interfaces;
std::map<Bytes, Destination>                Transport::_destinations;
std::map<Bytes, Transport::PathEntry>       Transport::_path_table;
std::set<Bytes>                             Transport::_seen_announces;
Transport::AnnounceCallback                 Transport::_on_announce = nullptr;

void Transport::register_interface(std::shared_ptr<InterfaceImpl> iface) {
    _interfaces.push_back(std::move(iface));
}

void Transport::deregister_interface(const std::shared_ptr<InterfaceImpl>& iface) {
    _interfaces.erase(std::remove(_interfaces.begin(), _interfaces.end(), iface),
                      _interfaces.end());
}

const std::vector<std::shared_ptr<InterfaceImpl>>& Transport::interfaces() {
    return _interfaces;
}

void Transport::register_destination(const Destination& dest) {
    _destinations.insert_or_assign(dest.hash(), dest);
}

void Transport::deregister_destination(const Destination& dest) {
    _destinations.erase(dest.hash());
}

Destination Transport::find_destination_from_hash(const Bytes& hash) {
    auto it = _destinations.find(hash);
    return it == _destinations.end() ? Destination{Type::NONE} : it->second;
}

void Transport::broadcast(const Bytes& raw, const std::shared_ptr<InterfaceImpl>& skip) {
    for (auto& iface : _interfaces) {
        if (iface == skip) continue;
        iface->send_outgoing(raw);
    }
}

bool Transport::has_path(const Bytes& destination_hash) {
    return _path_table.find(destination_hash) != _path_table.end();
}

uint8_t Transport::hops_to(const Bytes& destination_hash) {
    auto it = _path_table.find(destination_hash);
    return it == _path_table.end() ? 0 : it->second.hops;
}

const Transport::PathEntry* Transport::lookup_path(const Bytes& destination_hash) {
    auto it = _path_table.find(destination_hash);
    return it == _path_table.end() ? nullptr : &it->second;
}

void Transport::clear_paths() {
    _path_table.clear();
}

void Transport::on_announce(AnnounceCallback cb) {
    _on_announce = std::move(cb);
}

void Transport::reset() {
    _interfaces.clear();
    _destinations.clear();
    _path_table.clear();
    _seen_announces.clear();
    _on_announce = nullptr;
}

void Transport::inbound(const Bytes& raw, const Interface& iface) {
    Packet pkt(raw);
    if (!pkt.unpack()) return;

    if (pkt.packet_type() == Type::Packet::ANNOUNCE) {
        process_announce(pkt, iface);
        return;
    }

    if (pkt.packet_type() == Type::Packet::DATA) {
        auto it = _destinations.find(pkt.destination_hash());
        if (it != _destinations.end()) {
            Bytes plaintext = it->second.decrypt(pkt.data());
            if (!plaintext.empty()) it->second.receive(plaintext, pkt);
            return;
        }
        auto path = _path_table.find(pkt.destination_hash());
        if (path != _path_table.end() && path->second.via_interface) {
            Bytes forward = raw;
            if (forward.size() > 1) {
                uint8_t hops = forward.data()[1];
                forward[1] = static_cast<uint8_t>(hops + 1);
            }
            path->second.via_interface->send_outgoing(forward);
        }
    }
}

bool Transport::process_announce(const Packet& packet, const Interface& iface) {
    /* Dedup by packet hash. The hash covers the announce data (including its
     * random_hash field) so each emission is unique, but a relayed copy of
     * the same announce hashes identically and gets dropped here. Prevents
     * forward loops in mesh topologies. */
    if (!_seen_announces.insert(packet.get_hash()).second) return false;

    constexpr size_t KEY  = Type::Identity::KEYSIZE / 8;
    constexpr size_t NHL  = Type::Identity::NAME_HASH_LENGTH / 8;
    constexpr size_t RHL  = Type::Identity::RANDOM_HASH_LENGTH / 8;
    constexpr size_t SIGL = Type::Identity::SIGLENGTH / 8;
    constexpr size_t MIN  = KEY + NHL + RHL + SIGL;

    const Bytes& d = packet.data();
    if (d.size() < MIN) return false;

    Bytes public_key  = d.left(KEY);
    Bytes name_hash   = d.mid(KEY, NHL);
    Bytes random_hash = d.mid(KEY + NHL, RHL);
    Bytes signature   = d.mid(KEY + NHL + RHL, SIGL);
    Bytes app_data;
    if (d.size() > MIN) app_data = d.mid(MIN);

    Bytes signed_data;
    signed_data << packet.destination_hash() << public_key << name_hash << random_hash;
    if (!app_data.empty()) signed_data << app_data;

    Identity announced(false);
    announced.load_public_key(public_key);
    if (!announced.validate(signature, signed_data)) {
        DEBUGF("Transport: invalid announce signature for %s", packet.destination_hash().toHex().c_str());
        return false;
    }

    Bytes hash_material;
    hash_material << name_hash << announced.hash();
    Bytes expected = Identity::full_hash(hash_material).left(Type::Reticulum::TRUNCATED_HASHLENGTH / 8);
    if (expected != packet.destination_hash()) {
        DEBUGF("Transport: announce hash mismatch for %s", packet.destination_hash().toHex().c_str());
        return false;
    }

    Identity::remember(packet.get_hash(), packet.destination_hash(), public_key, app_data);

    auto via = iface.get() ? iface.get()->shared_from_this() : nullptr;
    PathEntry entry;
    entry.next_hop      = packet.destination_hash();
    entry.hops          = packet.hops();
    entry.timestamp     = Utilities::OS::time();
    entry.via_interface = via;
    _path_table.insert_or_assign(packet.destination_hash(), entry);

    if (_on_announce) _on_announce(packet.destination_hash(), announced, app_data);

    if (packet.hops() < Type::Transport::PATHFINDER_M) {
        Bytes forward = packet.raw();
        if (forward.size() > 1) {
            forward[1] = static_cast<uint8_t>(packet.hops() + 1);
        }
        broadcast(forward, via);
    }

    return true;
}

}
