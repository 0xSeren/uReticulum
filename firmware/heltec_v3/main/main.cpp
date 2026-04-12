/*
 * uReticulum on Heltec WiFi LoRa 32 V3
 *
 * Build (from this firmware/heltec_v3/ directory):
 *   nix develop ../..#  (or have idf.py + ESP-IDF v5.x in PATH)
 *   . $IDF_PATH/export.sh
 *   idf.py set-target esp32s3
 *   idf.py build
 *   idf.py -p /dev/ttyACM0 flash monitor
 *
 * What this app does:
 *   1. Brings up the SX1262 radio
 *   2. Creates a uReticulum Identity + Destination
 *   3. Registers the LoRa interface with Transport
 *   4. Spawns the Reticulum loop on a FreeRTOS task
 *   5. Announces the destination every 30 seconds
 */

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/reticulum.h"
#include "ureticulum/transport.h"

#include "lora_interface.h"

static const char* TAG = "app";

extern "C" void app_main() {
    ESP_LOGI(TAG, "uReticulum on Heltec V3 starting");

    /* Bring up the LoRa interface. */
    auto lora = HeltecV3::LoraInterface::create();
    if (!lora->start()) {
        ESP_LOGE(TAG, "LoRa interface failed to start, halting");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    RNS::Transport::register_interface(lora);

    /* Create our identity and a single SINGLE/IN destination so other
     * nodes can discover us via announces. */
    RNS::Identity identity;
    RNS::Destination dest(identity,
                          RNS::Type::Destination::IN,
                          RNS::Type::Destination::SINGLE,
                          "ureticulum",
                          "heltec_v3");
    RNS::Transport::register_destination(dest);

    ESP_LOGI(TAG, "identity hash: %s", identity.hash().toHex().c_str());
    ESP_LOGI(TAG, "destination hash: %s", dest.hash().toHex().c_str());

    /* Spawn the Reticulum task — drives Interface::loop() at 50 ms ticks
     * which the SX1262 LoraInterface uses to drain DIO1-flagged frames. */
    if (!RNS::Reticulum::start(/*tick_ms=*/50, /*stack_words=*/8192, /*priority=*/5)) {
        ESP_LOGE(TAG, "Reticulum::start failed");
    }

    /* Periodic announce loop runs from app_main itself. */
    while (true) {
        ESP_LOGI(TAG, "announcing %s", dest.hash().toHex().c_str());
        try {
            dest.announce(RNS::Bytes("hello from heltec v3"));
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "announce failed: %s", e.what());
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
