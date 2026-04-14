set(SRCS
    "main.cpp"
    "lora_interface.cpp"
    "radiolib_esp_idf_hal.cpp"
    "oled.cpp"
    "rnode_bridge.cpp"
    "rnode_ble_bridge.cpp"
    "wifi_sta.cpp"
    "tcp_interface.cpp"
    "ota_update.cpp"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        ureticulum
        driver
        esp_driver_i2c
        esp_driver_uart
        esp_timer
        jgromes__RadioLib
        bt
        nvs_flash
        esp_wifi
        esp_netif
        esp_http_client
        esp_https_ota
        app_update
)
