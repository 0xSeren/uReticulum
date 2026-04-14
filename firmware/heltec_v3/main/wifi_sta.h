#pragma once

#include <stdint.h>

namespace HeltecV3::WiFiSta {

    /* Connect to the AP whose SSID + PSK are stored in NVS namespace
     * "wifi" (keys "ssid" and "psk"). Falls back to Kconfig defaults
     * CONFIG_WIFI_DEFAULT_SSID / CONFIG_WIFI_DEFAULT_PSK if NVS is
     * empty. Returns true if connected within timeout_ms. */
    bool init(uint32_t timeout_ms = 15000);

    /* Store new credentials to NVS and reconnect. */
    bool set_credentials(const char* ssid, const char* psk);

    bool is_connected();

}
