/*
 * uReticulum on Heltec WiFi LoRa 32 V3
 *
 * Build + flash (from this firmware/heltec_v3/ directory):
 *   nix develop ../..
 *   idf.py build flash -p /dev/ttyUSB0
 */

#include <atomic>
#include <set>
#include <stdio.h>
#include <string>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "ureticulum/destination.h"
#include "ureticulum/identity.h"
#include "ureticulum/link.h"
#include "ureticulum/reticulum.h"
#include "ureticulum/transport.h"

#include "heltec_v3_pins.h"
#include "lora_interface.h"
#include "oled.h"
#include "rnode_ble_bridge.h"
#include "rnode_bridge.h"

static const char* TAG = "app";

namespace {
    std::atomic<uint32_t> g_announce_count{0};
    std::atomic<uint32_t> g_rx_count{0};
    std::atomic<uint32_t> g_link_tx{0};
    std::atomic<uint32_t> g_link_rx{0};

    /* Outbound link held so the main loop can poll it for status and fire
     * a one-shot hello payload once the LRPROOF lands. Written from the
     * announce callback (Transport task), read from the main loop. */
    std::shared_ptr<RNS::Link> g_out_link;
    std::atomic<bool> g_hello_sent{false};

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

        snprintf(line, sizeof(line), "Anc TX%-3u RX%-3u",
                 (unsigned)g_announce_count.load(),
                 (unsigned)g_rx_count.load());
        HeltecV3::Oled::print(6, 0, line);

        const char* link_state = "--";
        if (g_out_link) {
            switch (g_out_link->status()) {
                case RNS::Link::PENDING:   link_state = "pd"; break;
                case RNS::Link::HANDSHAKE: link_state = "hs"; break;
                case RNS::Link::ACTIVE:    link_state = "ok"; break;
                case RNS::Link::CLOSED:    link_state = "xx"; break;
            }
        }
        snprintf(line, sizeof(line), "Lnk %s TX%-3u RX%-3u",
                 link_state,
                 (unsigned)g_link_tx.load(),
                 (unsigned)g_link_rx.load());
        HeltecV3::Oled::print(7, 0, line);

        HeltecV3::Oled::flush();
    }

    uint64_t now_ms() { return (uint64_t)(esp_timer_get_time() / 1000); }
}

#if CONFIG_HELTEC_V3_MODE_RNODE
extern "C" void app_main() {
    /* RNode bridge mode: skip PM, OLED, Identity and Reticulum entirely.
     * The board is a dumb radio driven by Python RNS over UART0. */
    auto lora = HeltecV3::LoraInterface::create();
    if (!lora->start()) {
        /* No OLED, no log — just park the CPU. Host will time out. */
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    HeltecV3::RNodeBridge::run(lora);  /* never returns */
}
#elif CONFIG_HELTEC_V3_MODE_RNODE_BLE
extern "C" void app_main() {
    /* Same as RNode UART mode, but the host speaks KISS over a Nordic
     * UART Service GATT profile instead of USB serial. */
    auto lora = HeltecV3::LoraInterface::create();
    if (!lora->start()) {
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    HeltecV3::RNodeBleBridge::run(lora);  /* never returns */
}
#else
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

    /* Load or create a persistent identity. The 64-byte private key
     * (32 X25519 + 32 Ed25519) is stored in NVS so the destination
     * hash is stable across reboots — paths, bonds, and peer state
     * survive power cycles. */
    esp_err_t nvs_rc = nvs_flash_init();
    if (nvs_rc == ESP_ERR_NVS_NO_FREE_PAGES || nvs_rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    RNS::Identity identity(false);  /* don't create keys yet */
    {
        nvs_handle_t h;
        bool loaded = false;
        if (nvs_open("ureticulum", NVS_READONLY, &h) == ESP_OK) {
            size_t len = 0;
            if (nvs_get_blob(h, "id_prv", nullptr, &len) == ESP_OK && len == 64) {
                uint8_t key[64];
                nvs_get_blob(h, "id_prv", key, &len);
                RNS::Bytes kb(key, 64);
                if (identity.load_private_key(kb)) {
                    loaded = true;
                    ESP_LOGI(TAG, "loaded identity from NVS");
                }
            }
            nvs_close(h);
        }
        if (!loaded) {
            identity.createKeys();
            RNS::Bytes prv = identity.get_private_key();
            nvs_handle_t wh;
            if (nvs_open("ureticulum", NVS_READWRITE, &wh) == ESP_OK) {
                nvs_set_blob(wh, "id_prv", prv.data(), prv.size());
                nvs_commit(wh);
                nvs_close(wh);
                ESP_LOGI(TAG, "created + saved new identity to NVS");
            }
        }
    }

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

    /* Responder side: when a peer sends us a LINKREQUEST, validate it and
     * install a packet callback that logs + counts every inbound payload. */
    RNS::Transport::set_link_request_handler(
        [](const RNS::Destination& owner, const RNS::Bytes& req, const RNS::Packet& pkt) -> std::shared_ptr<RNS::Link> {
            auto link = RNS::Link::validate_request(owner, req, pkt);
            if (link) {
                ESP_LOGI(TAG, "link inbound accepted, hash=%s", link->hash().toHex().c_str());
                link->set_packet_callback([](const RNS::Bytes& data, const RNS::Link& l) {
                    g_link_rx.fetch_add(1);
                    std::string txt(reinterpret_cast<const char*>(data.data()), data.size());
                    ESP_LOGI(TAG, "link RX[%s] %zu bytes: %s",
                             l.hash().toHex().substr(0, 8).c_str(),
                             (size_t)data.size(), txt.c_str());
                });
            }
            return link;
        });

    /* Initiator side: when we hear an announce from a peer that isn't us
     * and we don't already have an outbound link open, construct an OUT
     * destination from their Identity and fire a LINKREQUEST. Once the
     * LRPROOF lands the main loop notices Link::ACTIVE and sends hello. */
    RNS::Bytes our_dst_hash = dest.hash();
    RNS::Transport::on_announce(
        [our_dst_hash](const RNS::Bytes& dh, const RNS::Identity& peer_id, const RNS::Bytes&) {
            g_rx_count.fetch_add(1);
            if (dh == our_dst_hash) return;
            if (g_out_link) return;
            ESP_LOGI(TAG, "heard peer %s, opening link", dh.toHex().c_str());
            RNS::Destination peer_dest(peer_id,
                                       RNS::Type::Destination::OUT,
                                       RNS::Type::Destination::SINGLE,
                                       "ureticulum",
                                       "heltec_v3");
            g_out_link = RNS::Link::request(peer_dest);
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

        /* One-shot hello payload once the outbound link reaches ACTIVE.
         * The link handshake happens on the Transport task; we poll it
         * here rather than sending from a callback so we don't need to
         * juggle non-const access to Link from the const-ref packet cb. */
        if (g_out_link && !g_hello_sent.load() && g_out_link->status() == RNS::Link::ACTIVE) {
            ESP_LOGI(TAG, "link ACTIVE, sending hello");
            try {
                std::string msg = "hello from " + id_hex.substr(0, 8);
                g_out_link->send(RNS::Bytes(msg));
                g_link_tx.fetch_add(1);
                g_hello_sent.store(true);
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "link send failed: %s", e.what());
            }
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
#endif  /* CONFIG_HELTEC_V3_MODE_RNODE */
