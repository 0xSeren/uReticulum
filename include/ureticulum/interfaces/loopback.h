#pragma once

#include <memory>

#include "ureticulum/interface.h"

namespace RNS { namespace Interfaces {

    /* In-process loopback. Used for host tests and as the testbed for the
     * core packet/identity path. Two LoopbackInterface instances paired with
     * pair() will deliver any send_outgoing on one as handle_incoming on the
     * other, synchronously. */
    class LoopbackInterface : public InterfaceImpl {
    public:
        static std::shared_ptr<LoopbackInterface> create(const char* name);
        static void pair(const std::shared_ptr<LoopbackInterface>& a,
                         const std::shared_ptr<LoopbackInterface>& b);

        void send_outgoing(const Bytes& data) override;

    private:
        LoopbackInterface(const char* name);
        std::weak_ptr<LoopbackInterface> _peer;
    };

}}
