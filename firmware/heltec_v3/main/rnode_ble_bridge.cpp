#include "sdkconfig.h"
#if CONFIG_BT_ENABLED

#include "rnode_ble_bridge.h"
#include "rnode_bridge.h"   /* RNodeKiss::attach / feed_byte / etc. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* Forward-declared here because NimBLE's private headers aren't always
 * exposed through the ESP-IDF component include path. */
extern "C" void ble_store_config_init(void);
extern "C" int  ble_store_util_status_rr(struct ble_store_status_event *event, void *arg);

#include "heltec_v3_pins.h"
#include "lora_interface.h"

/* Nordic UART Service — the de-facto standard for BLE serial bridges,
 * and what Python RNS's RNodeInterface looks for in BLE mode (see
 * RNS.Interfaces.RNodeInterface.BLEConnection.UART_SERVICE_UUID).
 *
 *   service : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   RX char : 6E400002-B5A3-F393-E0A9-E50E24DCCA9E  (host writes → us)
 *   TX char : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E  (we notify → host)
 *
 * NimBLE stores 128-bit UUIDs in little-endian byte order, so the
 * visible hex above appears reversed in these initializers. */
namespace {

    constexpr const char* TAG = "rnode_ble";

    static const ble_uuid128_t NUS_SERVICE_UUID = BLE_UUID128_INIT(
        0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
        0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
    static const ble_uuid128_t NUS_RX_CHAR_UUID  = BLE_UUID128_INIT(
        0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
        0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
    static const ble_uuid128_t NUS_TX_CHAR_UUID  = BLE_UUID128_INIT(
        0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
        0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

    /* Filled in by the GATT registration callback so the TX sink can
     * notify on the right attribute. */
    static uint16_t g_nus_tx_val_handle = 0;

    /* Connection handle of the currently-connected central, or
     * BLE_HS_CONN_HANDLE_NONE when nobody is connected. */
    static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;

    /* Negotiated MTU (minus 3 for the ATT notification header). BLE
     * defaults to 23 / 20 bytes; Python RNS's bleak negotiates up to
     * 247 / 244. We start at the minimum and raise on MTU exchange. */
    static uint16_t g_notify_mtu = 20;

    /* Pending "send CMD_READY on next main-loop tick" flag. We can't
     * emit notifications from inside the BLE_GAP_EVENT_SUBSCRIBE
     * handler: that fires on the NimBLE host task while it's still
     * processing the CCCD write, and calling ble_gattc_notify_custom
     * re-entrantly returns 0x0E (ATT Unlikely Error) back to the
     * central. Defer the emit to run()'s main loop. */
    static volatile bool g_pending_ready = false;

    /* RX ring between the NUS write callback (NimBLE host task) and
     * the main bridge loop that runs RNodeKiss::feed_byte. We can't
     * feed bytes directly from the access callback because feed_byte
     * may emit a response frame via send_frame -> ble_gattc_notify_
     * custom, and that re-enters the NimBLE GATT machinery while it's
     * still processing the incoming write. The resulting error
     * propagates back to the central as ATT 0x0E (Unlikely Error). */
    constexpr size_t RX_RING_SIZE = 2048;
    static uint8_t   g_rx_ring[RX_RING_SIZE];
    static volatile size_t g_rx_head = 0;  /* producer — NimBLE task */
    static volatile size_t g_rx_tail = 0;  /* consumer — bridge loop */

    /* Handle incoming writes on the RX characteristic by queuing each
     * byte into the RX ring. The main bridge loop feeds the KISS
     * parser from the other end. */
    static int nus_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt* ctxt, void* arg) {
        (void)conn_handle; (void)attr_handle; (void)arg;
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint8_t buf[512];
            uint16_t copied = 0;
            if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &copied) == 0) {
                for (uint16_t i = 0; i < copied; ++i) {
                    size_t next = (g_rx_head + 1) % RX_RING_SIZE;
                    if (next == g_rx_tail) break;  /* full — drop */
                    g_rx_ring[g_rx_head] = buf[i];
                    g_rx_head = next;
                }
            }
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Access callback for the notify-only TX characteristic. Reads
     * return an empty value (peer should subscribe for notifications
     * instead); writes are not permitted. */
    static int nus_tx_access_cb(uint16_t, uint16_t,
                                struct ble_gatt_access_ctxt* ctxt, void*) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return 0;
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    static const struct ble_gatt_chr_def nus_chars[] = {
        {
            .uuid       = &NUS_RX_CHAR_UUID.u,
            .access_cb  = nus_access_cb,
            .arg        = nullptr,
            .descriptors= nullptr,
            .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            .min_key_size = 0,
            .val_handle = nullptr,
            .cpfd       = nullptr,
        },
        {
            .uuid       = &NUS_TX_CHAR_UUID.u,
            .access_cb  = nus_tx_access_cb,
            .arg        = nullptr,
            .descriptors= nullptr,
            .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = 0,
            .val_handle = &g_nus_tx_val_handle,
            .cpfd       = nullptr,
        },
        { 0 },
    };

    static const struct ble_gatt_svc_def nus_svcs[] = {
        {
            .type            = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid            = &NUS_SERVICE_UUID.u,
            .includes        = nullptr,
            .characteristics = nus_chars,
        },
        { 0 },
    };

    /* Outbound KISS sink: chunk the frame into MTU-sized notifications
     * and push them to the subscribed central. Called synchronously
     * from RNodeKiss (which runs on the bridge task), so we don't need
     * our own locking here. */
    void ble_tx_sink(const uint8_t* data, size_t len) {
        if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
        size_t offset = 0;
        while (offset < len) {
            size_t chunk = len - offset;
            if (chunk > g_notify_mtu) chunk = g_notify_mtu;
            struct os_mbuf* om = ble_hs_mbuf_from_flat(data + offset, chunk);
            if (!om) break;
            int rc = ble_gattc_notify_custom(g_conn_handle, g_nus_tx_val_handle, om);
            if (rc != 0) break;
            offset += chunk;
        }
    }

    /* Forward decl for the advertising restart after disconnect. */
    void start_advertising();

    /* GAP event callback — handles connect / disconnect / mtu updates. */
    int gap_event_cb(struct ble_gap_event* event, void* arg) {
        (void)arg;
        if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
            ESP_LOGI(TAG, "ENC_CHANGE status=%d", event->enc_change.status);
        } else if (event->type == BLE_GAP_EVENT_PARING_COMPLETE) {
            ESP_LOGI(TAG, "PARING_COMPLETE status=%d", event->pairing_complete.status);
        } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
            ESP_LOGI(TAG, "DISCONNECT reason=%d", event->disconnect.reason);
        } else if (event->type == BLE_GAP_EVENT_PASSKEY_ACTION) {
            ESP_LOGI(TAG, "PASSKEY_ACTION action=%d", event->passkey.params.action);
            /* Just Works: auto-confirm the numeric compare. With
             * sm_io_cap=DISP_YES_NO the peer may elect NumCompare; we
             * can't actually display anything so we blindly accept. */
            if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
                struct ble_sm_io pk = {};
                pk.action  = BLE_SM_IOACT_NUMCMP;
                pk.numcmp_accept = 1;
                ble_sm_inject_io(event->passkey.conn_handle, &pk);
            }
        } else {
            ESP_LOGI(TAG, "GAP event type=%d", event->type);
        }
        switch (event->type) {
            case BLE_GAP_EVENT_CONNECT:
                if (event->connect.status == 0) {
                    g_conn_handle = event->connect.conn_handle;
                    g_notify_mtu  = 20;
                    /* Relax connection params so the link survives idle
                     * LoRa periods. Default supervision timeout is too
                     * tight (~2-6s) for traffic that may be silent for
                     * minutes between announces. */
                    struct ble_gap_upd_params p;
                    memset(&p, 0, sizeof(p));
                    p.itvl_min = 24;             /* 30ms */
                    p.itvl_max = 40;             /* 50ms */
                    p.latency  = 0;
                    p.supervision_timeout = 3000; /* 30s */
                    ble_gap_update_params(g_conn_handle, &p);
                } else {
                    start_advertising();
                }
                break;

            case BLE_GAP_EVENT_DISCONNECT:
                g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                g_notify_mtu  = 20;
                start_advertising();
                break;

            case BLE_GAP_EVENT_MTU:
                /* ATT MTU includes the 3-byte opcode+handle header, so
                 * the notify payload cap is mtu-3. */
                if (event->mtu.value > 3) {
                    g_notify_mtu = event->mtu.value - 3;
                }
                break;

            case BLE_GAP_EVENT_REPEAT_PAIRING: {
                /* A peer we've previously bonded with is re-pairing
                 * (e.g. after an NVS erase or host-side key removal).
                 * Delete the old bond and let the new pairing proceed. */
                struct ble_gap_conn_desc desc;
                ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
                ble_store_util_delete_peer(&desc.peer_id_addr);
                return BLE_GAP_REPEAT_PAIRING_RETRY;
            }

            case BLE_GAP_EVENT_SUBSCRIBE:
                if (event->subscribe.cur_notify) {
                    /* Defer the CMD_READY emit — see g_pending_ready. */
                    g_pending_ready = true;
                }
                break;
        }
        return 0;
    }

    void start_advertising() {
        struct ble_gap_adv_params adv_params = {};
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

        struct ble_hs_adv_fields fields = {};
        fields.flags       = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        fields.uuids128    = (ble_uuid128_t*)&NUS_SERVICE_UUID;
        fields.num_uuids128 = 1;
        fields.uuids128_is_complete = 1;
        fields.tx_pwr_lvl_is_present = 1;
        fields.tx_pwr_lvl  = BLE_HS_ADV_TX_PWR_LVL_AUTO;
        ble_gap_adv_set_fields(&fields);

        /* Stash the device name in the scan-response payload — there
         * isn't room for a long 128-bit UUID and a name in the single
         * 31-byte advertisement. */
        struct ble_hs_adv_fields rsp = {};
        const char* name = ble_svc_gap_device_name();
        rsp.name         = (uint8_t*)name;
        rsp.name_len     = strlen(name);
        rsp.name_is_complete = 1;
        ble_gap_adv_rsp_set_fields(&rsp);

        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                          &adv_params, gap_event_cb, nullptr);
    }

    /* Build the device name from the MAC so multiple boards don't
     * collide on the air. Matches the "RNode XXXX" convention upstream
     * RNode firmware uses so Python RNS's port filter (`ble://RNode`)
     * finds us. */
    void set_device_name() {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_BT);
        char name[16];
        snprintf(name, sizeof(name), "RNode %02X%02X", mac[4], mac[5]);
        ble_svc_gap_device_name_set(name);
    }

    void on_sync() {
        int rc = ble_hs_util_ensure_addr(0);
        if (rc != 0) return;
        set_device_name();
        start_advertising();
    }

    void nimble_host_task(void*) {
        nimble_port_run();  /* returns when nimble_port_stop() is called */
        nimble_port_freertos_deinit();
    }

}

namespace HeltecV3::RNodeBleBridge {

void run(std::shared_ptr<LoraInterface> lora) {
    /* Don't silence ESP_LOG in BLE mode — UART0 carries only debug
     * output since the KISS stream runs over the BLE link instead. */
    ESP_LOGI(TAG, "rnode_ble: entering run()");

    /* Pin CPU at 160 MHz, no light sleep — BLE controller is happier
     * without clock transitions under it, and RNode bridge mode isn't
     * power-sensitive. */
    esp_pm_config_t pm_cfg = {};
    pm_cfg.max_freq_mhz       = 160;
    pm_cfg.min_freq_mhz       = 160;
    pm_cfg.light_sleep_enable = false;
    esp_pm_configure(&pm_cfg);

    /* LED solid-on as a crude "we're alive" indicator. */
    gpio_reset_pin((gpio_num_t)HELTEC_V3_LED);
    gpio_set_direction((gpio_num_t)HELTEC_V3_LED, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)HELTEC_V3_LED, 1);

    /* NVS is required for the NimBLE bond store. Without this the host
     * crashes inside ble_store on the first pair attempt. */
    esp_err_t nvs_rc = nvs_flash_init();
    if (nvs_rc == ESP_ERR_NVS_NO_FREE_PAGES || nvs_rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Bring up NimBLE before attaching the KISS core — RNodeKiss::
     * attach installs a LoRa RX callback that can fire instantly, and
     * we want the BLE TX sink already wired by the time that happens. */
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc;
    rc = ble_gatts_count_cfg(nus_svcs);
    ESP_LOGI(TAG, "ble_gatts_count_cfg = %d", rc);
    rc = ble_gatts_add_svcs(nus_svcs);
    ESP_LOGI(TAG, "ble_gatts_add_svcs  = %d", rc);
    rc = ble_gatts_start();
    ESP_LOGI(TAG, "ble_gatts_start     = %d (tx_val_handle=%u)", rc, g_nus_tx_val_handle);

    ble_hs_cfg.sync_cb          = on_sync;

    /* Pairing / bonding config. Python RNS BLEConnection refuses to
     * connect to any device that isn't already bonded at the BlueZ
     * level, so we need a security manager that can complete pairing.
     * Just Works (no IO capability, MITM off) keeps things usable on a
     * headless board — the user still has to approve the pair from
     * `bluetoothctl pair <addr>` once, and BlueZ remembers the bond
     * thereafter. */
    /* LE Secure Connections pairing, Just Works. Python RNS's
     * BLEConnection requires the device to be bonded at the OS level
     * before it will attempt to use it, so we need to complete SMP
     * and store the keys. */
    ble_hs_cfg.sm_io_cap        = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding       = 1;
    ble_hs_cfg.sm_mitm          = 0;
    ble_hs_cfg.sm_sc            = 1;
    ble_hs_cfg.sm_our_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    /* Persist bond state (keys, IRK, etc.) to NVS via the default
     * NimBLE config store. */
    ble_store_config_init();

    nimble_port_freertos_init(nimble_host_task);

    RNodeKiss::attach(std::move(lora), ble_tx_sink);

    /* Main loop just polls the LoRa driver for RX packets; inbound
     * host writes come through the GATT access callback from the
     * NimBLE host task. */
    while (true) {
        if (g_pending_ready && g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            g_pending_ready = false;
            RNodeKiss::indicate_ready();
        }
        /* Drain any bytes the NUS write callback queued. */
        while (g_rx_tail != g_rx_head) {
            uint8_t b = g_rx_ring[g_rx_tail];
            g_rx_tail = (g_rx_tail + 1) % RX_RING_SIZE;
            RNodeKiss::feed_byte(b);
        }
        RNodeKiss::poll_lora();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}

#endif  /* CONFIG_BT_ENABLED */
