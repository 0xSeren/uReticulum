/*
 * uReticulum on Heltec WiFi LoRa 32 V3
 *
 * Build + flash (from this firmware/heltec_v3/ directory):
 *   nix develop ../..
 *   idf.py build flash -p /dev/ttyUSB0
 */

#include <atomic>
#include <stdio.h>
#include <string>

#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/reticulum.h"
#include "ureticulum/transport.h"

#include "lora_interface.h"
#include "oled.h"

static const char* TAG = "app";

namespace {
    std::atomic<uint32_t> g_announce_count{0};
    std::atomic<uint32_t> g_rx_count{0};

    /* CPU idle-time tracking. We sample the IDLE task's runtime counter
     * at the start of each OLED refresh window and compute the delta vs
     * total wall-clock in that window. The result is an idle percentage
     * that tracks the real duty cycle of the CPU between interrupts. */
    uint64_t g_last_idle_us  = 0;
    uint64_t g_last_total_us = 0;

    /* ESP32-S3 is dual-core; each core has its own IDLE task. We sum both
     * idle task runtime counters, then divide by 2 and by the total runtime
     * window to get a "per-core average" idle percentage where 100% means
     * both cores are fully idle. */
    uint32_t measure_idle_percent() {
        TaskStatus_t status[10];
        uint32_t total_runtime = 0;
        UBaseType_t n = uxTaskGetSystemState(status, 10, &total_runtime);
        uint32_t idle_runtime = 0;
        int idle_tasks_found = 0;
        for (UBaseType_t i = 0; i < n; ++i) {
            if (status[i].pcTaskName &&
                (strcmp(status[i].pcTaskName, "IDLE0") == 0 ||
                 strcmp(status[i].pcTaskName, "IDLE1") == 0 ||
                 strcmp(status[i].pcTaskName, "IDLE")  == 0)) {
                idle_runtime += status[i].ulRunTimeCounter;
                idle_tasks_found++;
            }
        }

        uint32_t delta_idle  = idle_runtime  - (uint32_t)g_last_idle_us;
        uint32_t delta_total = total_runtime - (uint32_t)g_last_total_us;
        g_last_idle_us  = idle_runtime;
        g_last_total_us = total_runtime;

        if (delta_total == 0 || idle_tasks_found == 0) return 0;
        return (uint32_t)((uint64_t)delta_idle * 100 / (delta_total * idle_tasks_found));
    }

    /* Render a concise 8-line status page. */
    void render_status(const std::string& id_hex,
                       const std::string& dst_hex,
                       uint32_t idle_pct) {
        HeltecV3::Oled::clear();

        HeltecV3::Oled::print(0, 0, "uReticulum");
        HeltecV3::Oled::hline(9);

        char line[24];
        snprintf(line, sizeof(line), "ID  %.12s", id_hex.c_str());
        HeltecV3::Oled::print(2, 0, line);
        snprintf(line, sizeof(line), "DST %.12s", dst_hex.c_str());
        HeltecV3::Oled::print(3, 0, line);

        /* Row 4: CPU idle % — proxy for power savings. 100% idle ≈ MCU
         * spending all its time in WFI / light sleep between interrupts. */
        snprintf(line, sizeof(line), "CPU idle %u%%", (unsigned)idle_pct);
        HeltecV3::Oled::print(4, 0, line);

        HeltecV3::Oled::print(5, 0, "915.0 SF9 BW125 US");

        snprintf(line, sizeof(line), "TX %-4u  RX %-4u",
                 (unsigned)g_announce_count.load(),
                 (unsigned)g_rx_count.load());
        HeltecV3::Oled::print(7, 0, line);

        HeltecV3::Oled::flush();
    }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "uReticulum on Heltec V3 starting");

    /* Dynamic frequency scaling: CPU clocks down to 40 MHz when idle,
     * back up to 160 MHz when there's work. Saves most of the achievable
     * MCU power without the I2C clock glitches that true light sleep
     * causes on the OLED bus. True light_sleep_enable=true produces
     * visible OLED flicker because APB transitions between 40 and 80 MHz
     * corrupt the SSD1306 command stream. */
    esp_pm_config_t pm_config = {};
    pm_config.max_freq_mhz       = 160;
    pm_config.min_freq_mhz       = 40;
    pm_config.light_sleep_enable = false;
    esp_err_t pm_rc = esp_pm_configure(&pm_config);
    ESP_LOGI(TAG, "esp_pm_configure = %d (DFS 40-160 MHz)", (int)pm_rc);

    /* Bring up the OLED first so we can show status during bring-up. */
    bool oled_up = HeltecV3::Oled::init();
    if (oled_up) {
        HeltecV3::Oled::clear();
        HeltecV3::Oled::print(0, 0, "uReticulum");
        HeltecV3::Oled::hline(9);
        HeltecV3::Oled::print(2, 0, "Booting...");
        HeltecV3::Oled::flush();
    }

    /* Bring up the LoRa interface. */
    auto lora = HeltecV3::LoraInterface::create();
    if (!lora->start()) {
        ESP_LOGE(TAG, "LoRa interface failed to start, halting");
        if (oled_up) {
            HeltecV3::Oled::set_line(4, "LoRa init FAIL");
            HeltecV3::Oled::flush();
        }
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    RNS::Transport::register_interface(lora);

    if (oled_up) {
        HeltecV3::Oled::set_line(4, "LoRa online");
        HeltecV3::Oled::flush();
    }

    /* Create identity + SINGLE/IN destination. */
    RNS::Identity identity;
    RNS::Destination dest(identity,
                          RNS::Type::Destination::IN,
                          RNS::Type::Destination::SINGLE,
                          "ureticulum",
                          "heltec_v3");
    RNS::Transport::register_destination(dest);

    std::string id_hex  = identity.hash().toHex();
    std::string dst_hex = dest.hash().toHex();
    ESP_LOGI(TAG, "identity hash: %s", id_hex.c_str());
    ESP_LOGI(TAG, "destination hash: %s", dst_hex.c_str());

    /* Global on_announce hook — bumps RX count whenever Transport validates
     * an incoming announce. Good sanity check that the radio receive path
     * is healthy. */
    RNS::Transport::on_announce([](const RNS::Bytes& dh, const RNS::Identity&, const RNS::Bytes&) {
        (void)dh;
        g_rx_count.fetch_add(1);
    });

    if (!RNS::Reticulum::start(/*tick_ms=*/50, /*stack_words=*/8192, /*priority=*/5)) {
        ESP_LOGE(TAG, "Reticulum::start failed");
    }

    /* Initial status render. */
    if (oled_up) render_status(id_hex, dst_hex, 0);

    /* Loop with 5 s ticks so idle % samples frequently. Announce every 6
     * ticks (30 s). */
    uint32_t tick = 0;
    while (true) {
        if (tick % 6 == 0) {
            ESP_LOGI(TAG, "announcing %s", dst_hex.c_str());
            try {
                dest.announce(RNS::Bytes("hello from heltec v3"));
                g_announce_count.fetch_add(1);
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "announce failed: %s", e.what());
            }
        }

        uint32_t idle = measure_idle_percent();
        ESP_LOGI(TAG, "tick=%u idle=%u%% TX=%u RX=%u",
                 (unsigned)tick, (unsigned)idle,
                 (unsigned)g_announce_count.load(),
                 (unsigned)g_rx_count.load());

        if (oled_up) render_status(id_hex, dst_hex, idle);

        tick++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
