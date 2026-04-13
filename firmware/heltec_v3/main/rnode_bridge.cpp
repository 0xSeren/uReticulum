#include "rnode_bridge.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "heltec_v3_pins.h"
#include "lora_interface.h"

/* Transport-agnostic RNode KISS core. Parser, protocol state, and
 * command handlers live in HeltecV3::RNodeKiss so both the UART
 * (RNodeBridge) and BLE (RNodeBleBridge) transports can share them;
 * each transport installs its own TxSink. Protocol constants match
 * upstream RNode_Firmware/Framing.h so Python RNS RNodeInterface
 * treats us as a stock Heltec V3 RNode. */
namespace HeltecV3::RNodeKiss {

    constexpr uint8_t FEND  = 0xC0;
    constexpr uint8_t FESC  = 0xDB;
    constexpr uint8_t TFEND = 0xDC;
    constexpr uint8_t TFESC = 0xDD;

    constexpr uint8_t CMD_UNKNOWN     = 0xFE;
    constexpr uint8_t CMD_DATA        = 0x00;
    constexpr uint8_t CMD_FREQUENCY   = 0x01;
    constexpr uint8_t CMD_BANDWIDTH   = 0x02;
    constexpr uint8_t CMD_TXPOWER     = 0x03;
    constexpr uint8_t CMD_SF          = 0x04;
    constexpr uint8_t CMD_CR          = 0x05;
    constexpr uint8_t CMD_RADIO_STATE = 0x06;
    constexpr uint8_t CMD_RADIO_LOCK  = 0x07;
    constexpr uint8_t CMD_DETECT      = 0x08;
    constexpr uint8_t CMD_READY       = 0x0F;
    constexpr uint8_t CMD_STAT_RX     = 0x21;
    constexpr uint8_t CMD_STAT_TX     = 0x22;
    constexpr uint8_t CMD_STAT_RSSI   = 0x23;
    constexpr uint8_t CMD_STAT_SNR    = 0x24;
    constexpr uint8_t CMD_BLINK       = 0x30;
    constexpr uint8_t CMD_RANDOM      = 0x40;
    constexpr uint8_t CMD_BOARD       = 0x47;
    constexpr uint8_t CMD_PLATFORM    = 0x48;
    constexpr uint8_t CMD_MCU         = 0x49;
    constexpr uint8_t CMD_FW_VERSION  = 0x50;
    constexpr uint8_t CMD_ERROR       = 0x90;

    constexpr uint8_t DETECT_REQ      = 0x73;
    constexpr uint8_t DETECT_RESP     = 0x46;

    constexpr uint8_t RADIO_STATE_OFF = 0x00;
    constexpr uint8_t RADIO_STATE_ON  = 0x01;

    constexpr uint8_t ERROR_INITRADIO = 0x01;

    /* Board identity bytes RNode advertises to the host. Match
     * RNode_Firmware/Boards.h so Python RNS draws the right platform
     * label. MAJ/MIN picked to match a recent upstream release — Python
     * RNS doesn't refuse old versions, it just logs them. */
    constexpr uint8_t MAJ_VERS        = 0x01;
    constexpr uint8_t MIN_VERS        = 0x55;
    constexpr uint8_t PLATFORM_ESP32  = 0x80;
    constexpr uint8_t MCU_ESP32       = 0x81;
    constexpr uint8_t BOARD_HELTEC_V3 = 0x3A;

    /* KISS max payload. CMD_DATA frames from the host can be up to
     * Reticulum MTU + RNode header byte; allow a bit of slack. */
    constexpr size_t FRAME_MAX = 600;

    /* Live radio parameter cache. Python RNS uses query forms (4-byte
     * zero payload for frequency/bandwidth, 0xFF for sf/cr/txpower/
     * state) to read current settings back and validate them against
     * what it just configured. We track what was last set so we can
     * answer queries with real values, not with the literal payload
     * bytes the host sent. Defaults match the Heltec V3 boot settings
     * in lora_interface.cpp. */
    struct RadioCfg {
        uint32_t freq_hz = 915000000u;
        uint32_t bw_hz   = 125000u;
        uint8_t  sf      = 9;
        uint8_t  cr      = 7;
        int8_t   txp     = 14;
        uint8_t  state   = RADIO_STATE_ON;
    };

    /* Module-local state. This file is the only writer, all writes run
     * on the transport task so no locking. */
    static std::shared_ptr<LoraInterface> g_lora;
    static TxSink g_tx = nullptr;
    static RadioCfg g_cfg;

    static bool    in_frame    = false;
    static bool    escape      = false;
    static uint8_t current_cmd = CMD_UNKNOWN;
    static uint8_t frame_buf[FRAME_MAX];
    static size_t  frame_len   = 0;

    /* Write a KISS-framed response. Escapes payload bytes that look
     * like FEND or FESC so the host parser won't mis-segment us. */
    static void send_frame(uint8_t cmd, const uint8_t* data, size_t len) {
        if (!g_tx) return;
        uint8_t buf[FRAME_MAX * 2 + 4];
        size_t  o = 0;
        buf[o++] = FEND;
        buf[o++] = cmd;
        for (size_t i = 0; i < len && o + 2 < sizeof(buf); ++i) {
            uint8_t b = data[i];
            if      (b == FEND) { buf[o++] = FESC; buf[o++] = TFEND; }
            else if (b == FESC) { buf[o++] = FESC; buf[o++] = TFESC; }
            else                { buf[o++] = b; }
        }
        buf[o++] = FEND;
        g_tx(buf, o);
    }

    static void send_u8(uint8_t cmd, uint8_t value) {
        send_frame(cmd, &value, 1);
    }

    static void send_error(uint8_t code) { send_u8(CMD_ERROR, code); }

    static void send_freq() {
        uint8_t b[4] = {
            (uint8_t)((g_cfg.freq_hz >> 24) & 0xFF),
            (uint8_t)((g_cfg.freq_hz >> 16) & 0xFF),
            (uint8_t)((g_cfg.freq_hz >>  8) & 0xFF),
            (uint8_t)( g_cfg.freq_hz        & 0xFF),
        };
        send_frame(CMD_FREQUENCY, b, 4);
    }
    static void send_bw() {
        uint8_t b[4] = {
            (uint8_t)((g_cfg.bw_hz >> 24) & 0xFF),
            (uint8_t)((g_cfg.bw_hz >> 16) & 0xFF),
            (uint8_t)((g_cfg.bw_hz >>  8) & 0xFF),
            (uint8_t)( g_cfg.bw_hz        & 0xFF),
        };
        send_frame(CMD_BANDWIDTH, b, 4);
    }

    /* Python RNS expects STAT_RSSI + STAT_SNR immediately before a
     * CMD_DATA frame so it can attach them to the received packet. */
    static void send_stat_rssi(float rssi_dbm) {
        int32_t v = (int32_t)rssi_dbm + 157;
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        send_u8(CMD_STAT_RSSI, (uint8_t)v);
    }
    static void send_stat_snr(float snr_db) {
        int32_t v = (int32_t)(snr_db * 4.0f);
        if (v < -128) v = -128;
        if (v >  127) v =  127;
        send_u8(CMD_STAT_SNR, (uint8_t)(int8_t)v);
    }

    static void handle_frame() {
        switch (current_cmd) {
            case CMD_DATA: {
                if (frame_len == 0) break;
                if (g_lora) g_lora->send_raw(frame_buf, frame_len);
                break;
            }
            case CMD_FREQUENCY: {
                if (frame_len == 4) {
                    uint32_t hz = ((uint32_t)frame_buf[0] << 24)
                                | ((uint32_t)frame_buf[1] << 16)
                                | ((uint32_t)frame_buf[2] <<  8)
                                |  (uint32_t)frame_buf[3];
                    if (hz != 0) {
                        if (g_lora && g_lora->set_frequency_mhz((float)hz / 1000000.0f)) {
                            g_cfg.freq_hz = hz;
                        } else {
                            send_error(ERROR_INITRADIO);
                        }
                    }
                    send_freq();
                }
                break;
            }
            case CMD_BANDWIDTH: {
                if (frame_len == 4) {
                    uint32_t hz = ((uint32_t)frame_buf[0] << 24)
                                | ((uint32_t)frame_buf[1] << 16)
                                | ((uint32_t)frame_buf[2] <<  8)
                                |  (uint32_t)frame_buf[3];
                    if (hz != 0) {
                        if (g_lora && g_lora->set_bandwidth_khz((float)hz / 1000.0f)) {
                            g_cfg.bw_hz = hz;
                        } else {
                            send_error(ERROR_INITRADIO);
                        }
                    }
                    send_bw();
                }
                break;
            }
            case CMD_SF: {
                if (frame_len == 1) {
                    if (frame_buf[0] != 0xFF) {
                        if (g_lora && g_lora->set_spreading_factor(frame_buf[0])) {
                            g_cfg.sf = frame_buf[0];
                        } else {
                            send_error(ERROR_INITRADIO);
                        }
                    }
                    send_u8(CMD_SF, g_cfg.sf);
                }
                break;
            }
            case CMD_CR: {
                if (frame_len == 1) {
                    if (frame_buf[0] != 0xFF) {
                        if (g_lora && g_lora->set_coding_rate(frame_buf[0])) {
                            g_cfg.cr = frame_buf[0];
                        } else {
                            send_error(ERROR_INITRADIO);
                        }
                    }
                    send_u8(CMD_CR, g_cfg.cr);
                }
                break;
            }
            case CMD_TXPOWER: {
                if (frame_len == 1) {
                    if (frame_buf[0] != 0xFF) {
                        int8_t dbm = (int8_t)frame_buf[0];
                        if (g_lora && g_lora->set_tx_power_dbm(dbm)) {
                            g_cfg.txp = dbm;
                        } else {
                            send_error(ERROR_INITRADIO);
                        }
                    }
                    send_u8(CMD_TXPOWER, (uint8_t)g_cfg.txp);
                }
                break;
            }
            case CMD_RADIO_STATE: {
                if (frame_len == 1) {
                    if (frame_buf[0] != 0xFF) {
                        bool want = (frame_buf[0] == RADIO_STATE_ON);
                        if (g_lora && g_lora->set_radio_online(want)) {
                            g_cfg.state = want ? RADIO_STATE_ON : RADIO_STATE_OFF;
                        } else {
                            send_error(ERROR_INITRADIO);
                        }
                    }
                    send_u8(CMD_RADIO_STATE, g_cfg.state);
                }
                break;
            }
            case CMD_DETECT: {
                if (frame_len == 1 && frame_buf[0] == DETECT_REQ) {
                    send_u8(CMD_DETECT, DETECT_RESP);
                }
                break;
            }
            case CMD_READY: {
                indicate_ready();
                break;
            }
            case CMD_FW_VERSION: {
                uint8_t v[2] = { MAJ_VERS, MIN_VERS };
                send_frame(CMD_FW_VERSION, v, 2);
                break;
            }
            case CMD_PLATFORM: {
                send_u8(CMD_PLATFORM, PLATFORM_ESP32);
                break;
            }
            case CMD_MCU: {
                send_u8(CMD_MCU, MCU_ESP32);
                break;
            }
            case CMD_BOARD: {
                send_u8(CMD_BOARD, BOARD_HELTEC_V3);
                break;
            }
            case CMD_RANDOM: {
                uint8_t r = (uint8_t)(esp_random() & 0xFF);
                send_u8(CMD_RANDOM, r);
                break;
            }
            case CMD_BLINK:
            case CMD_RADIO_LOCK:
            case CMD_STAT_RX:
            case CMD_STAT_TX:
                /* No-op / stub: we parse but don't implement. */
                break;
            default:
                break;
        }
    }

    static void on_lora_rx(const uint8_t* data, size_t len, float rssi, float snr) {
        /* Match upstream RNode ordering: STAT_RSSI + STAT_SNR first,
         * then the DATA frame. Python RNS RNodeInterface.processIncoming
         * reads them in this order and caches them for the next
         * CMD_DATA. */
        send_stat_rssi(rssi);
        send_stat_snr(snr);
        send_frame(CMD_DATA, data, len);
    }

    void attach(std::shared_ptr<LoraInterface> lora, TxSink tx) {
        g_lora = std::move(lora);
        g_tx   = tx;
        if (g_lora) g_lora->set_raw_rx_callback(on_lora_rx);
    }

    /* Transport run loops poll this to drain any LoRa RX queued by the
     * DIO1 ISR. The LoraInterface::loop() implementation calls back
     * into on_lora_rx for each received packet, which frames it and
     * emits via the installed TxSink. */
    void poll_lora() {
        if (g_lora) g_lora->loop();
    }

    /* KISS parser. KISS frames share boundary FENDs — the closing FEND
     * of frame N is the same byte as the opening FEND of frame N+1, so
     * a stream like
     *   FEND CMD_A data FEND CMD_B data FEND
     * contains TWO frames, not one. We process the previous frame on
     * FEND, then stay in_frame=true and clear cmd/buf so the following
     * bytes start a new frame. Empty frames between back-to-back FENDs
     * are no-ops, which matches upstream RNode behaviour. */
    void feed_byte(uint8_t b) {
        if (b == FEND) {
            if (in_frame && current_cmd != CMD_UNKNOWN) handle_frame();
            in_frame    = true;
            escape      = false;
            current_cmd = CMD_UNKNOWN;
            frame_len   = 0;
            return;
        }
        if (!in_frame) return;

        if (current_cmd == CMD_UNKNOWN) {
            current_cmd = b;
            return;
        }

        if (b == FESC) { escape = true; return; }
        if (escape) {
            if      (b == TFEND) b = FEND;
            else if (b == TFESC) b = FESC;
            escape = false;
        }

        if (frame_len < FRAME_MAX) frame_buf[frame_len++] = b;
    }

    void indicate_ready() { send_u8(CMD_READY, 0x01); }

}

/* UART transport — installs the KISS core over UART0. */
namespace {

    constexpr uart_port_t UART = UART_NUM_0;

    /* TxSink hook: ship one full KISS frame through UART0. Block until
     * the TX FIFO drains so the host sees responses arrive promptly
     * even when we emit several frames back-to-back. Without the wait,
     * the IDF driver's TX ISR runs lazily and frames trickle out over
     * many ms — long enough for Python RNS's 250 ms validation window
     * to expire before all bytes have actually left the chip. */
    void uart_tx_sink(const uint8_t* data, size_t len) {
        uart_write_bytes(UART, (const char*)data, len);
        uart_wait_tx_done(UART, portMAX_DELAY);
    }

}

namespace HeltecV3::RNodeBridge {

void run(std::shared_ptr<LoraInterface> lora) {
    /* Shut up every ESP_LOG tag so log output can't corrupt the KISS
     * stream the host is decoding. */
    esp_log_level_set("*", ESP_LOG_NONE);

    /* Pin CPU at 160 MHz, no light sleep. The XTAL UART clock makes the
     * baud rate independent of CPU frequency, but disabling DFS removes
     * any chance of a glitch during the clock transition. RNode mode
     * isn't power-sensitive (host-driven, mains power) so the savings
     * from DFS aren't worth the risk. */
    esp_pm_config_t pm_cfg = {};
    pm_cfg.max_freq_mhz       = 160;
    pm_cfg.min_freq_mhz       = 160;
    pm_cfg.light_sleep_enable = false;
    esp_pm_configure(&pm_cfg);

    /* LED solid-on to show we reached run(). */
    gpio_reset_pin((gpio_num_t)HELTEC_V3_LED);
    gpio_set_direction((gpio_num_t)HELTEC_V3_LED, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)HELTEC_V3_LED, 1);

    /* Pin UART0 to the XTAL (40 MHz) clock source. The default after
     * bootloader is APB, which scales with the CPU frequency under DFS.
     * When the chip drops to 40 MHz APB the UART baud generator drifts
     * by ~4x and inbound bytes get misframed — exactly the symptom we
     * spent half a day chasing where Python RNS only saw 2 of 4 detect
     * responses come back. XTAL clock is independent of DFS so the baud
     * stays correct regardless of CPU frequency.
     *
     * uart_param_config must run BEFORE driver_install. The driver
     * picks up whatever clock source is configured at install time. */
    uart_config_t cfg = {};
    cfg.baud_rate  = 115200;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_XTAL;
    uart_param_config(UART, &cfg);
    uart_driver_install(UART, 2048, 2048, 0, nullptr, 0);

    RNodeKiss::attach(std::move(lora), uart_tx_sink);
    RNodeKiss::indicate_ready();

    /* Read one byte at a time with a long-ish poll timeout. The
     * read-256-with-10ms-timeout approach occasionally tore frames
     * apart across read calls when the host sent a burst right while
     * we were busy sending a response. Single-byte reads keep the
     * parser tightly synchronised with the wire. */
    while (true) {
        uint8_t b;
        int n = uart_read_bytes(UART, &b, 1, pdMS_TO_TICKS(2));
        if (n == 1) RNodeKiss::feed_byte(b);
        RNodeKiss::poll_lora();
    }
}

}
