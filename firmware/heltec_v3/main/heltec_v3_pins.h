#pragma once

/* ESP32-S3 + SX1262 LoRa pinout.
 *
 * Current target: Heltec WiFi LoRa 32 V3.
 * Swap the values below for other boards:
 *
 *   Heltec V3:        NSS=8  SCK=9  MOSI=10 MISO=11 RST=12 BUSY=13 DIO1=14
 *   LILYGO T3-S3:     NSS=7  SCK=5  MOSI=6  MISO=3  RST=8  BUSY=34 DIO1=33
 *   Seeed XIAO+Wio:   NSS=41 SCK=7  MOSI=9  MISO=8  RST=42 BUSY=40 DIO1=39 */

#define HELTEC_V3_LORA_NSS    8
#define HELTEC_V3_LORA_SCK    9
#define HELTEC_V3_LORA_MOSI   10
#define HELTEC_V3_LORA_MISO   11
#define HELTEC_V3_LORA_RST    12
#define HELTEC_V3_LORA_BUSY   13
#define HELTEC_V3_LORA_DIO1   14

/* On-board SSD1306 OLED. */
#define HELTEC_V3_OLED_SDA    17
#define HELTEC_V3_OLED_SCL    18
#define HELTEC_V3_OLED_RST    21

#define HELTEC_V3_LED         35
#define HELTEC_V3_BUTTON_PRG  0

/* Battery voltage via ADC1 channel 0 (GPIO1).
 * On-board 100K/100K resistor divider → multiply ADC reading by 2.
 * ADC_CTRL (GPIO37) must be driven HIGH to enable the divider. */
#define HELTEC_V3_VBAT_ADC    1
#define HELTEC_V3_VBAT_CTRL   37

/* LoRa parameters — sourced from menuconfig (uReticulum LoRa menu).
 * Frequency is stored as MHz*10 in Kconfig (int) to avoid float. */
#include "sdkconfig.h"
#define HELTEC_V3_LORA_FREQ_MHZ          (CONFIG_LORA_FREQ_MHZ_X10 / 10.0f)
#define HELTEC_V3_LORA_BANDWIDTH_KHZ     ((float)CONFIG_LORA_BANDWIDTH_KHZ)
#define HELTEC_V3_LORA_SPREADING_FACTOR  CONFIG_LORA_SPREADING_FACTOR
#define HELTEC_V3_LORA_CODING_RATE       CONFIG_LORA_CODING_RATE
#define HELTEC_V3_LORA_TX_POWER_DBM      CONFIG_LORA_TX_POWER_DBM
#define HELTEC_V3_LORA_PREAMBLE_LENGTH   CONFIG_LORA_PREAMBLE_LENGTH
