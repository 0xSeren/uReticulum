#pragma once

/* Heltec WiFi LoRa 32 V3 — ESP32-S3 + SX1262
 * https://heltec.org/project/wifi-lora-32-v3/
 *
 * SX1262 sub-GHz LoRa radio is wired to dedicated SPI pins on the
 * ESP32-S3. The DIO1 pin is the radio's interrupt line. */

#define HELTEC_V3_LORA_NSS    8
#define HELTEC_V3_LORA_SCK    9
#define HELTEC_V3_LORA_MOSI   10
#define HELTEC_V3_LORA_MISO   11
#define HELTEC_V3_LORA_RST    12
#define HELTEC_V3_LORA_BUSY   13
#define HELTEC_V3_LORA_DIO1   14

/* On-board SSD1306 OLED — not used by Phase 9 bring-up but defined here
 * so the future display interface knows the pin map. */
#define HELTEC_V3_OLED_SDA    17
#define HELTEC_V3_OLED_SCL    18
#define HELTEC_V3_OLED_RST    21

#define HELTEC_V3_LED         35
#define HELTEC_V3_BUTTON_PRG  0

/* Default LoRa parameters for ISM 868 MHz operation. Adjust per region. */
#define HELTEC_V3_LORA_FREQ_MHZ          868.0
#define HELTEC_V3_LORA_BANDWIDTH_KHZ     125.0
#define HELTEC_V3_LORA_SPREADING_FACTOR  9
#define HELTEC_V3_LORA_CODING_RATE       7
#define HELTEC_V3_LORA_TX_POWER_DBM      14
#define HELTEC_V3_LORA_PREAMBLE_LENGTH   8
