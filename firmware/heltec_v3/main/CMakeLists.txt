set(SRCS
    "main.cpp"
    "lora_interface.cpp"
    "radiolib_esp_idf_hal.cpp"
    "oled.cpp"
    "rnode_bridge.cpp"
)

set(REQS
    ureticulum
    driver
    esp_driver_i2c
    esp_driver_uart
    esp_timer
    jgromes__RadioLib
)

if(CONFIG_HELTEC_V3_MODE_RNODE_BLE)
    list(APPEND SRCS "rnode_ble_bridge.cpp")
    list(APPEND REQS bt nvs_flash)
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${REQS}
)
