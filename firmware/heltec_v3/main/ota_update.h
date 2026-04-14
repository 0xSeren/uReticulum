#pragma once

namespace HeltecV3::OtaUpdate {

    /* Pull firmware from the given HTTP URL (or the URL stored in NVS
     * namespace "ota", key "url" if nullptr). Writes to the inactive
     * OTA partition. Returns true if OTA succeeded and a reboot is
     * needed. */
    bool pull(const char* url = nullptr);

    /* Store a new OTA URL in NVS. */
    void set_url(const char* url);

}
