#include "ureticulum/transport.h"

#include <algorithm>

#include "ureticulum/identity.h"
#include "ureticulum/link.h"
#include "ureticulum/log.h"
#include "ureticulum/os.h"
#include "ureticulum/packet.h"

namespace RNS {

std::recursive_mutex                        Transport::_mutex;
std::vector<std::shared_ptr<InterfaceImpl>> Transport::_interfaces;
std::map<Bytes, Destination>                Transport::_destinations;
std::map<Bytes, Transport::PathEntry>       Transport::_path_table;
std::map<Bytes, std::shared_ptr<Link>>      Transport::_links;
std::set<Bytes>                             Transport::_seen_announces;
Transport::AnnounceCallback                 Transport::_on_announce = nullptr;
Transport::LinkRequestCallback              Transport::_on_link_request = nullptr;

using Lock = std::lock_guard<std::recursive_mutex>;

void Transport::register_interface(std::shared_ptr<InterfaceImpl> iface) {
    Lock g(_mutex);
    _interfaces.push_back(std::move(iface));
}

void Transport::deregister_interface(const std::shared_ptr<InterfaceImpl>& iface) {
    Lock g(_mutex);
    _interfaces.erase(std::remove(_interfaces.begin(), _interfaces.end(), iface),
                      _interfaces.end());
}

const std::vector<std::shared_ptr<InterfaceImpl>>& Transport::interfaces() {
    /* Returns a reference to internal state — caller must coordinate access.
     * Used by Reticulum::run_once for the loop pump, which is single-threaded
     * by design. */
    return _interfaces;
}

void Transport::register_destination(const Destination& dest) {
    Lock g(_mutex);
    _destinations.insert_or_assign(dest.hash(), dest);
}

void Transport::deregister_destination(const Destination& dest) {
    Lock g(_mutex);
    _destinations.erase(dest.hash());
}

Destination Transport::find_destination_from_hash(const Bytes& hash) {
    Lock g(_mutex);
    auto it = _destinations.find(hash);
    return it == _destinations.end() ? Destination{Type::NONE} : it->second;
}

void Transport::register_link(const std::shared_ptr<Link>& link) {
    if (!link) return;
    Lock g(_mutex);
    _links.insert_or_assign(link->hash(), link);
}

void Transport::deregister_link(const std::shared_ptr<Link>& link) {
    if (!link) return;
    Lock g(_mutex);
    _links.erase(link->hash());
}

std::shared_ptr<Link> Transport::find_link(const Bytes& link_hash) {
    Lock g(_mutex);
    auto it = _links.find(link_hash);
    return it == _links.end() ? nullptr : it->second;
}

void Transport::set_link_request_handler(LinkRequestCallback cb) {
    Lock g(_mutex);
    _on_link_request = std::move(cb);
}

void Transport::broadcast(const Bytes& raw, const std::shared_ptr<InterfaceImpl>& skip) {
    /* Snapshot the interface list under the lock so we don't hold it while
     * calling into interface implementations (which may call back into
     * Transport::inbound on a loopback interface). The recursive mutex would
     * also tolerate this, but dropping the lock reduces hold time and avoids
     * head-of-line blocking against other tasks. */
    std::vector<std::shared_ptr<InterfaceImpl>> snapshot;
    {
        Lock g(_mutex);
        snapshot = _interfaces;
    }
    for (auto& iface : snapshot) {
        if (iface == skip) continue;
        iface->send_outgoing(raw);
    }
}

bool Transport::has_path(const Bytes& destination_hash) {
    Lock g(_mutex);
    return _path_table.find(destination_hash) != _path_table.end();
}

uint8_t Transport::hops_to(const Bytes& destination_hash) {
    Lock g(_mutex);
    auto it = _path_table.find(destination_hash);
    return it == _path_table.end() ? 0 : it->second.hops;
}

const Transport::PathEntry* Transport::lookup_path(const Bytes& destination_hash) {
    /* Returns a raw pointer into the path table — only safe under the
     * understanding that callers coordinate against concurrent writes.
     * For multi-task safety prefer has_path / hops_to which return by value. */
    Lock g(_mutex);
    auto it = _path_table.find(destination_hash);
    return it == _path_table.end() ? nullptr : &it->second;
}

void Transport::clear_paths() {
    Lock g(_mutex);
    _path_table.clear();
}

void Transport::on_announce(AnnounceCallback cb) {
    Lock g(_mutex);
    _on_announce = std::move(cb);
}

void Transport::reset() {
    Lock g(_mutex);
    _interfaces.clear();
    _destinations.clear();
    _path_table.clear();
    _links.clear();
    _seen_announces.clear();
    _on_announce = nullptr;
    _on_link_request = nullptr;
}

void Transport::inbound(const Bytes& raw, const Interface& iface) {
    Lock g(_mutex);

    Packet pkt(raw);
    if (!pkt.unpack()) return;

    if (pkt.packet_type() == Type::Packet::ANNOUNCE) {
        process_announce(pkt, iface);
        return;
    }

    if (pkt.packet_type() == Type::Packet::LINKREQUEST) {
        auto dit = _destinations.find(pkt.destination_hash());
        if (dit == _destinations.end() || !_on_link_request) return;
        Destination dest = dit->second;
        LinkRequestCallback cb = _on_link_request;
        Bytes data = pkt.data();
        auto link = cb(dest, data, pkt);
        if (link) _links.insert_or_assign(link->hash(), link);
        return;
    }

    if (pkt.packet_type() == Type::Packet::PROOF && pkt.context() == Type::Packet::LRPROOF) {
        auto lit = _links.find(pkt.destination_hash());
        if (lit != _links.end()) lit->second->on_proof(pkt);
        return;
    }

    if (pkt.packet_type() == Type::Packet::DATA) {
        auto it = _destinations.find(pkt.destination_hash());
        if (it != _destinations.end()) {
            Destination d = it->second;
            Bytes plaintext = d.decrypt(pkt.data());
            if (!plaintext.empty()) d.receive(plaintext, pkt);
            return;
        }
        auto lit = _links.find(pkt.destination_hash());
        if (lit != _links.end()) {
            lit->second->on_inbound(pkt);
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
    /* Called only from inbound() which already holds _mutex. */
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
