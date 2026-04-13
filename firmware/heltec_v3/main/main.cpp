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

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/reticulum.h"
#include "ureticulum/transport.h"

#include "heltec_v3_pins.h"
#include "lora_interface.h"
#include "oled.h"

static const char* TAG = "app";

namespace {
    std::atomic<uint32_t> g_announce_count{0};
    std::atomic<uint32_t> g_rx_count{0};

    /* ISR-set flag for the PRG button (GPIO0, active LOW, external pullup). */
    volatile bool g_button_pressed = false;
    void IRAM_ATTR on_button_isr(void*) { g_button_pressed = true; }

    /* Display-on timeout: OLED stays visible for this many ms after the
     * user last pressed the button, then suspends. With the panel off
     * the I2C bus is idle and APB clock glitches from light sleep
     * wake/sleep transitions can't corrupt visible pixels. */
    constexpr uint32_t DISPLAY_ON_MS = 15000;
    uint64_t g_display_off_at_ms = 0;

    uint64_t g_last_idle_us  = 0;
    uint64_t g_last_total_us = 0;

    /* ESP32-S3 dual-core idle percentage averaged across both cores.
     * 100% = both IDLE tasks got all the CPU time in the sample window. */
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

        snprintf(line, sizeof(line), "CPU idle %u%%", (unsigned)idle_pct);
        HeltecV3::Oled::print(4, 0, line);

        HeltecV3::Oled::print(5, 0, "915.0 SF9 BW125 US");

        snprintf(line, sizeof(line), "TX %-4u  RX %-4u",
                 (unsigned)g_announce_count.load(),
                 (unsigned)g_rx_count.load());
        HeltecV3::Oled::print(7, 0, line);

        HeltecV3::Oled::flush();
    }

    uint64_t now_ms() { return (uint64_t)(esp_timer_get_time() / 1000); }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "uReticulum on Heltec V3 starting");

    /* Dynamic frequency scaling only. True light_sleep_enable=true causes
     * the 3.3V rail to dip 50-100 mV on every wake/sleep transition as
     * the MCU's current draw changes, and the Heltec V3 OLED sits on the
     * same rail — result is visible flicker. DFS alone still saves most
     * of the MCU power (CPU clocks down to 40 MHz when idle) without
     * touching the rail. A future rev could shorten I2C traces, add a
     * decoupling cap, or power the OLED from a separate LDO — then we
     * can re-enable light sleep for the last 10 mA of savings. */
    esp_pm_config_t pm_config = {};
    pm_config.max_freq_mhz       = 160;
    pm_config.min_freq_mhz       = 40;
    pm_config.light_sleep_enable = false;
    esp_err_t pm_rc = esp_pm_configure(&pm_config);
    ESP_LOGI(TAG, "esp_pm_configure = %d (DFS 40-160 MHz, light sleep off)", (int)pm_rc);

    /* PRG button on GPIO0 — internal pull-up, falling-edge interrupt for
     * press detection while awake, low-level wake source for pulling the
     * MCU out of light sleep. */
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = (1ULL << HELTEC_V3_BUTTON_PRG);
    btn_cfg.mode         = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    btn_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn_cfg.intr_type    = GPIO_INTR_NEGEDGE;
    gpio_config(&btn_cfg);
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add((gpio_num_t)HELTEC_V3_BUTTON_PRG, on_button_isr, nullptr);
    gpio_wakeup_enable((gpio_num_t)HELTEC_V3_BUTTON_PRG, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    /* Bring up the OLED so the user can see the boot sequence. */
    bool oled_up = HeltecV3::Oled::init();
    if (oled_up) {
        HeltecV3::Oled::clear();
        HeltecV3::Oled::print(0, 0, "uReticulum");
        HeltecV3::Oled::hline(9);
        HeltecV3::Oled::print(2, 0, "Booting...");
        HeltecV3::Oled::flush();
    }

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

    RNS::Transport::on_announce([](const RNS::Bytes& dh, const RNS::Identity&, const RNS::Bytes&) {
        (void)dh;
        g_rx_count.fetch_add(1);
    });

    if (!RNS::Reticulum::start(/*tick_ms=*/50, /*stack_words=*/8192, /*priority=*/5)) {
        ESP_LOGE(TAG, "Reticulum::start failed");
    }

    /* Display stays on for DISPLAY_ON_MS after boot so the user can read
     * the identity hash. */
    if (oled_up) {
        render_status(id_hex, dst_hex, 0);
        g_display_off_at_ms = now_ms() + DISPLAY_ON_MS;
    }

    uint32_t tick = 0;
    while (true) {
        uint64_t t = now_ms();

        /* Button press → wake display. */
        if (g_button_pressed) {
            g_button_pressed = false;
            ESP_LOGI(TAG, "button pressed, waking display");
            if (oled_up && HeltecV3::Oled::is_suspended()) {
                HeltecV3::Oled::resume();
            }
            g_display_off_at_ms = t + DISPLAY_ON_MS;
        }

        /* Display timeout → suspend OLED. */
        if (oled_up && !HeltecV3::Oled::is_suspended() && t >= g_display_off_at_ms) {
            ESP_LOGI(TAG, "display timeout, suspending OLED");
            HeltecV3::Oled::suspend();
        }

        /* Announce once at boot (tick==0) then every 5 minutes. Real
         * Reticulum applications announce far less often — once at boot,
         * on network-state change, and maybe every few hours for path
         * freshness. 5 min here is a compromise for bring-up testing so
         * the RX counter on the peer moves at a pace you can see. */
        if (tick == 0 || tick % 300 == 0) {
            ESP_LOGI(TAG, "announcing %s", dst_hex.c_str());
            try {
                dest.announce(RNS::Bytes("hello from heltec v3"));
                g_announce_count.fetch_add(1);
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "announce failed: %s", e.what());
            }
        }

        /* Log idle % every 5 ticks; redraw OLED only when it's visible. */
        if (tick % 5 == 0) {
            uint32_t idle = measure_idle_percent();
            ESP_LOGI(TAG, "tick=%u idle=%u%% TX=%u RX=%u disp=%s",
                     (unsigned)tick, (unsigned)idle,
                     (unsigned)g_announce_count.load(),
                     (unsigned)g_rx_count.load(),
                     HeltecV3::Oled::is_suspended() ? "off" : "on");
            if (oled_up && !HeltecV3::Oled::is_suspended()) {
                render_status(id_hex, dst_hex, idle);
            }
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
