#pragma once

#include <memory>

#include "ureticulum/interface.h"

class EspIdfHal;
class SX1262;

namespace HeltecV3 {

    /* uReticulum LoRa interface backed by RadioLib's SX1262 driver. RX is
     * triggered by DIO1 interrupt; the ISR sets a notification that the
     * interface task drains in loop(). */
    class LoraInterface : public RNS::InterfaceImpl {
    public:
        /* Public so std::shared_ptr can construct/destruct via new/delete. */
        LoraInterface();
        ~LoraInterface() override;

        static std::shared_ptr<LoraInterface> create();

        bool start() override;
        void stop()  override;
        void loop()  override;
        void send_outgoing(const RNS::Bytes& data) override;
        std::string toString() const override { return "LoraInterface[heltec_v3]"; }

    private:
        EspIdfHal*  _hal   = nullptr;
        SX1262*     _radio = nullptr;
    };

}
