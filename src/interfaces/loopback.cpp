#include "ureticulum/interfaces/loopback.h"

namespace RNS { namespace Interfaces {

LoopbackInterface::LoopbackInterface(const char* name) : InterfaceImpl(name) {
    _online = true;
}

std::shared_ptr<LoopbackInterface> LoopbackInterface::create(const char* name) {
    return std::shared_ptr<LoopbackInterface>(new LoopbackInterface(name));
}

void LoopbackInterface::pair(const std::shared_ptr<LoopbackInterface>& a,
                             const std::shared_ptr<LoopbackInterface>& b) {
    a->_peer = b;
    b->_peer = a;
}

void LoopbackInterface::send_outgoing(const Bytes& data) {
    _txb += data.size();
    if (auto p = _peer.lock()) p->handle_incoming(data);
}

}}
