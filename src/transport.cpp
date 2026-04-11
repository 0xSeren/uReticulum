#include "ureticulum/transport.h"

#include "ureticulum/destination.h"

namespace RNS {

Transport::InboundCallback Transport::_on_inbound = nullptr;

void Transport::set_inbound_callback(InboundCallback cb) {
    _on_inbound = cb;
}

void Transport::inbound(const Bytes& data, const Interface& iface) {
    if (_on_inbound) _on_inbound(data, iface);
}

Destination Transport::find_destination_from_hash(const Bytes&) {
    return Destination{Type::NONE};
}

}
