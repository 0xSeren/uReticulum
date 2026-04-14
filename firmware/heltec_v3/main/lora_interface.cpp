#include "lora_interface.h"

#include <RadioLib.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_random.h"
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

    /* Upstream RNode firmware enables CRC on all LoRa packets. Without
     * this, a real RNode will silently drop our frames (CRC absent →
     * check fails on the receiver). */
    _radio->setCRC(1);  /* SX126x: 0=off, 1=on. Matches upstream RNode. */

    _radio->setDio1Action(on_dio1);
    state = _radio->startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "SX1262 startReceive failed: %d", state);
        return false;
    }
    _online = true;
    _radio_on = true;
    ESP_LOGI(TAG, "SX1262 listening on %.1f MHz SF%d BW%.0f",
             HELTEC_V3_LORA_FREQ_MHZ,
             HELTEC_V3_LORA_SPREADING_FACTOR,
             HELTEC_V3_LORA_BANDWIDTH_KHZ);
    return true;
}

void LoraInterface::stop() {
    if (_radio) _radio->standby();
    _online = false;
    _radio_on = false;
}

void LoraInterface::loop() {
    if (!g_packet_pending) return;
    g_packet_pending = false;

    size_t len = _radio->getPacketLength();
    /* RNode frames have an extra header byte prepended by upstream
     * RNode_Firmware, so a raw RX can be up to MTU+1. Keep a small
     * headroom on the read buffer. */
    constexpr size_t MAX_FRAME = Type::Reticulum::MTU + 32;
    if (len == 0 || len > MAX_FRAME) {
        _radio->startReceive();
        return;
    }
    uint8_t buf[MAX_FRAME];
    int state = _radio->readData(buf, len);
    if (state == RADIOLIB_ERR_NONE && len > 1) {
        /* Strip the RNode header byte (first byte) before handing the
         * payload to Reticulum or the raw-RX callback. For the raw
         * (RNode bridge) path we keep the header since the host's
         * RNodeInterface expects to strip it itself. */
        if (_raw_rx) {
            float rssi = _radio->getRSSI();
            float snr  = _radio->getSNR();
            _raw_rx(buf, len, rssi, snr);
        } else {
            this->handle_incoming(Bytes(buf + 1, len - 1));
        }
    } else {
        ESP_LOGW(TAG, "readData failed: %d", state);
    }
    _radio->startReceive();
}

void LoraInterface::send_outgoing(const RNS::Bytes& data) {
    if (!_radio) return;
    /* Prepend the RNode header byte that real RNode firmware adds on
     * the wire.  Without this, a stock RNode strips the first byte
     * (treating it as the header) and corrupts the Reticulum frame.
     * The header is a random nibble in the upper 4 bits; the lower 4
     * bits carry flags (FLAG_SPLIT for multi-part packets, which we
     * never generate). */
    uint8_t buf[Type::Reticulum::MTU + 32];
    buf[0] = (uint8_t)(esp_random() & 0xF0);
    size_t len = data.size();
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
    memcpy(buf + 1, data.data(), len);
    _txb += len;
    int state = _radio->transmit(buf, len + 1);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "transmit failed: %d", state);
    }
    _radio->startReceive();
}

void LoraInterface::send_raw(const uint8_t* data, size_t len) {
    if (!_radio || !_radio_on) return;
    _txb += len;
    int state = _radio->transmit(const_cast<uint8_t*>(data), len);
    if (state != RADIOLIB_ERR_NONE) {
        /* Logging in RNode mode would corrupt the KISS stream on UART0,
         * so suppress output here — the bridge reports errors via KISS. */
    }
    _radio->startReceive();
}

bool LoraInterface::set_frequency_mhz(float mhz) {
    if (!_radio) return false;
    return _radio->setFrequency(mhz) == RADIOLIB_ERR_NONE;
}

bool LoraInterface::set_bandwidth_khz(float khz) {
    if (!_radio) return false;
    return _radio->setBandwidth(khz) == RADIOLIB_ERR_NONE;
}

bool LoraInterface::set_spreading_factor(int sf) {
    if (!_radio) return false;
    return _radio->setSpreadingFactor((uint8_t)sf) == RADIOLIB_ERR_NONE;
}

bool LoraInterface::set_coding_rate(int cr) {
    if (!_radio) return false;
    return _radio->setCodingRate((uint8_t)cr) == RADIOLIB_ERR_NONE;
}

bool LoraInterface::set_tx_power_dbm(int dbm) {
    if (!_radio) return false;
    return _radio->setOutputPower((int8_t)dbm) == RADIOLIB_ERR_NONE;
}

bool LoraInterface::set_radio_online(bool online) {
    if (!_radio) return false;
    if (online == _radio_on) return true;
    int state;
    if (online) {
        state = _radio->startReceive();
    } else {
        state = _radio->standby();
    }
    if (state != RADIOLIB_ERR_NONE) return false;
    _radio_on = online;
    return true;
}

}
