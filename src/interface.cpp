#include "ureticulum/interface.h"

#include "ureticulum/transport.h"

namespace RNS {

void InterfaceImpl::handle_incoming(const Bytes& data) {
    _rxb += data.size();
    Interface iface(shared_from_this());
    Transport::inbound(data, iface);
}

}
