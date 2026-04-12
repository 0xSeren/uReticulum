#include "lora_interface.h"

#include <RadioLib.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "heltec_v3_pins.h"
#include "radiolib_esp_idf_hal.h"
#include "ureticulum/type.h"

using namespace RNS;

static const char* TAG = "lora";

namespace {
    /* Set by the SX1262 DIO1 ISR. The interface loop() polls and drains. */
    volatile bool g_packet_pending = false;
    void IRAM_ATTR on_dio1() {
        g_packet_pending = true;
    }
}

namespace HeltecV3 {

LoraInterface::LoraInterface() : RNS::InterfaceImpl("heltec_v3_lora") {}

LoraInterface::~LoraInterface() {
    delete _radio;
    delete _hal;
}

std::shared_ptr<LoraInterface> LoraInterface::create() {
    return std::shared_ptr<LoraInterface>(new LoraInterface());
}

bool LoraInterface::start() {
    _hal = new EspIdfHal(HELTEC_V3_LORA_SCK, HELTEC_V3_LORA_MISO, HELTEC_V3_LORA_MOSI,
                         HELTEC_V3_LORA_NSS, /*spi_host=*/SPI2_HOST);
    _radio = new SX1262(new Module(_hal,
                                   HELTEC_V3_LORA_NSS,
                                   HELTEC_V3_LORA_DIO1,
                                   HELTEC_V3_LORA_RST,
                                   HELTEC_V3_LORA_BUSY));

    /* TCXO voltage: Heltec V3 uses a 32 MHz TCXO powered from SX1262 DIO3
     * at 1.8V. Passing 0.0f disables TCXO entirely → SPI_CMD_TIMEOUT on
     * the first command because the radio has no reference clock. The
     * last bool enables SX1262 DIO2 as RF switch, which Heltec V3 uses
     * for TX/RX path steering. */
    int state = _radio->begin(HELTEC_V3_LORA_FREQ_MHZ,
                              HELTEC_V3_LORA_BANDWIDTH_KHZ,
                              HELTEC_V3_LORA_SPREADING_FACTOR,
                              HELTEC_V3_LORA_CODING_RATE,
                              RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                              HELTEC_V3_LORA_TX_POWER_DBM,
                              HELTEC_V3_LORA_PREAMBLE_LENGTH,
                              1.8f,
                              true);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "SX1262 begin failed: %d", state);
        return false;
    }

    _radio->setDio1Action(on_dio1);
    state = _radio->startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "SX1262 startReceive failed: %d", state);
        return false;
    }
    _online = true;
    ESP_LOGI(TAG, "SX1262 listening on %.1f MHz SF%d BW%.0f",
             HELTEC_V3_LORA_FREQ_MHZ,
             HELTEC_V3_LORA_SPREADING_FACTOR,
             HELTEC_V3_LORA_BANDWIDTH_KHZ);
    return true;
}

void LoraInterface::stop() {
    if (_radio) _radio->standby();
    _online = false;
}

void LoraInterface::loop() {
    if (!g_packet_pending) return;
    g_packet_pending = false;

    size_t len = _radio->getPacketLength();
    if (len == 0 || len > Type::Reticulum::MTU) {
        _radio->startReceive();
        return;
    }
    uint8_t buf[Type::Reticulum::MTU];
    int state = _radio->readData(buf, len);
    if (state == RADIOLIB_ERR_NONE) {
        this->handle_incoming(Bytes(buf, len));
    } else {
        ESP_LOGW(TAG, "readData failed: %d", state);
    }
    _radio->startReceive();
}

void LoraInterface::send_outgoing(const RNS::Bytes& data) {
    if (!_radio) return;
    _txb += data.size();
    int state = _radio->transmit((uint8_t*)data.data(), data.size());
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "transmit failed: %d", state);
    }
    _radio->startReceive();
}

}
