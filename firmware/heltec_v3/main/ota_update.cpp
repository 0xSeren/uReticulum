#include "ota_update.h"

#include <string.h>

#include "esp_https_ota.h"
#include "esp_log.h"
#include "nvs.h"

static const char* TAG = "ota";

namespace HeltecV3::OtaUpdate {

bool pull(const char* url) {
    static char stored_url[256] = {};

    if (!url) {
        nvs_handle_t h;
        if (nvs_open("ota", NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(stored_url);
            nvs_get_str(h, "url", stored_url, &len);
            nvs_close(h);
        }
#ifdef CONFIG_OTA_DEFAULT_URL
        if (stored_url[0] == '\0')
            strncpy(stored_url, CONFIG_OTA_DEFAULT_URL, sizeof(stored_url) - 1);
#endif
        url = stored_url;
    }

    if (!url || url[0] == '\0') {
        ESP_LOGW(TAG, "no OTA URL configured");
        return false;
    }

    ESP_LOGI(TAG, "pulling firmware from %s", url);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = url;
    http_cfg.timeout_ms = 30000;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_err_t rc = esp_https_ota(&ota_cfg);
    if (rc == ESP_OK) {
        ESP_LOGI(TAG, "OTA succeeded, reboot to activate");
        return true;
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(rc));
        return false;
    }
}

void set_url(const char* url) {
    nvs_handle_t h;
    if (nvs_open("ota", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "url", url);
        nvs_commit(h);
        nvs_close(h);
    }
}

}
