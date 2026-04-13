#pragma once

#include <memory>

namespace HeltecV3 {

    class LoraInterface;

    namespace RNodeBleBridge {

        /* Enter RNode-over-BLE bridge mode. Brings up NimBLE, advertises
         * a Nordic UART Service GATT profile, and pipes KISS frames
         * through it to the shared RNodeKiss core. Never returns. The
         * caller must not start Reticulum; in bridge mode the SX1262 is
         * driven entirely by the host. */
        [[noreturn]] void run(std::shared_ptr<LoraInterface> lora);

    }

}
