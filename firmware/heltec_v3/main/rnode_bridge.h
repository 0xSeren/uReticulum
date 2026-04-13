#pragma once

#include <memory>
#include <stddef.h>
#include <stdint.h>

namespace HeltecV3 {

    class LoraInterface;

    /* Transport-agnostic RNode KISS core. Parser, protocol state, and
     * command handlers live here. The UART (`RNodeBridge`) and BLE
     * (`RNodeBleBridge`) wrappers own the I/O and plug themselves in as
     * a TX sink — RNodeKiss never touches UART or BLE directly. */
    namespace RNodeKiss {

        /* TX callback that the caller installs. Called synchronously from
         * the parser task whenever a KISS frame needs to go out to the
         * host. Must be reentrant-safe with respect to feed_byte() on the
         * same task. */
        using TxSink = void (*)(const uint8_t* data, size_t len);

        /* Wire up the LoRa driver (for CMD_DATA TX and CMD_FREQUENCY etc
         * reconfiguration) and the transport's TX sink. Call once, before
         * feed_byte() or indicate_ready(). */
        void attach(std::shared_ptr<LoraInterface> lora, TxSink tx);

        /* Push one inbound byte from the host into the KISS parser.
         * Frame handling, responses, and LoRa TX all fire synchronously
         * from here. */
        void feed_byte(uint8_t b);

        /* Emit a one-shot CMD_READY so the host's post-connect timeout
         * doesn't fire before it receives any frames. */
        void indicate_ready();

        /* Transports call this from their main loop to drain any LoRa
         * RX queued by the DIO1 ISR. */
        void poll_lora();

    }

    namespace RNodeBridge {

        /* Enter UART RNode bridge mode. Silences ESP_LOG on UART0,
         * installs a raw RX callback on the LoRa interface, drives UART0
         * as a KISS command channel, and never returns. The caller must
         * not start Reticulum; in RNode mode the SX1262 is driven by the
         * host. */
        [[noreturn]] void run(std::shared_ptr<LoraInterface> lora);

    }

}
