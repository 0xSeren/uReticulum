#include "wifi_sta.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "sdkconfig.h"

static const char* TAG = "wifi";

namespace {
    EventGroupHandle_t g_wifi_events = nullptr;
    constexpr int BIT_CONNECTED = BIT0;
    bool g_connected = false;

    void on_wifi_event(void*, esp_event_base_t base, int32_t id, void*) {
        if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
            g_connected = false;
            xEventGroupClearBits(g_wifi_events, BIT_CONNECTED);
            ESP_LOGI(TAG, "disconnected, reconnecting...");
            esp_wifi_connect();
        }
    }

    void on_ip_event(void*, esp_event_base_t base, int32_t id, void* data) {
        if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            auto* ev = (ip_event_got_ip_t*)data;
            ESP_LOGI(TAG, "connected, ip=" IPSTR, IP2STR(&ev->ip_info.ip));
            g_connected = true;
            xEventGroupSetBits(g_wifi_events, BIT_CONNECTED);
        }
    }
}

namespace HeltecV3::WiFiSta {

bool init(uint32_t timeout_ms) {
    char ssid[33] = {};
    char psk[65]  = {};

    /* Try NVS first. */
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(ssid);
        nvs_get_str(h, "ssid", ssid, &len);
        len = sizeof(psk);
        nvs_get_str(h, "psk", psk, &len);
        nvs_close(h);
    }

    /* Fall back to Kconfig defaults. */
#ifdef CONFIG_WIFI_DEFAULT_SSID
    if (ssid[0] == '\0') strncpy(ssid, CONFIG_WIFI_DEFAULT_SSID, sizeof(ssid) - 1);
#endif
#ifdef CONFIG_WIFI_DEFAULT_PSK
    if (psk[0] == '\0') strncpy(psk, CONFIG_WIFI_DEFAULT_PSK, sizeof(psk) - 1);
#endif

    if (ssid[0] == '\0') {
        ESP_LOGI(TAG, "no WiFi credentials, skipping");
        return false;
    }

    g_wifi_events = xEventGroupCreate();

    esp_err_t err;
    err = esp_netif_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "netif_init: %s", esp_err_to_name(err)); return false; }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { ESP_LOGE(TAG, "event_loop: %s", esp_err_to_name(err)); return false; }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "wifi_init: %s", esp_err_to_name(err)); return false; }

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, nullptr);

    wifi_config_t sta_cfg = {};
    strncpy((char*)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char*)sta_cfg.sta.password, psk, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();

    ESP_LOGI(TAG, "connecting to '%s'...", ssid);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(g_wifi_events, BIT_CONNECTED,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT_CONNECTED) != 0;
}

bool set_credentials(const char* ssid, const char* psk) {
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "psk", psk);
    nvs_commit(h);
    nvs_close(h);

    wifi_config_t sta_cfg = {};
    memcpy(sta_cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(sta_cfg.sta.password, psk, strlen(psk));
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_disconnect();
    esp_wifi_connect();
    return true;
}

bool is_connected() { return g_connected; }

}
