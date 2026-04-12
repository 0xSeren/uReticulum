cmake_minimum_required(VERSION 3.16)

# Heltec WiFi LoRa 32 V3 firmware project for uReticulum.
# Build with ESP-IDF v5.x:
#   nix develop ../..#
#   . $IDF_PATH/export.sh
#   idf.py set-target esp32s3
#   idf.py build flash monitor

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ureticulum_heltec_v3)
