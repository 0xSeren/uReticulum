#include "oled.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "heltec_v3_pins.h"
#include "glcdfont.h"

static const char* TAG = "oled";

namespace {
    /* 128x64 framebuffer in SSD1306 column-page layout: 128 columns × 8
     * pages, each page a vertical slice of 8 pixels (LSB at top). */
    uint8_t g_fb[1024];

    i2c_master_bus_handle_t g_bus      = nullptr;
    i2c_master_dev_handle_t g_dev      = nullptr;
    bool                    g_ready    = false;
    bool                    g_suspended = false;

    constexpr uint8_t SSD1306_ADDR = 0x3C;

    bool i2c_cmd(uint8_t cmd) {
        uint8_t buf[2] = {0x00, cmd};
        return i2c_master_transmit(g_dev, buf, 2, 50) == ESP_OK;
    }

    bool i2c_data_row(const uint8_t* data, size_t len) {
        uint8_t stack[129];
        if (len > sizeof(stack) - 1) return false;
        stack[0] = 0x40;
        memcpy(stack + 1, data, len);
        return i2c_master_transmit(g_dev, stack, len + 1, 50) == ESP_OK;
    }

    void draw_char(int px, int py, char c) {
        if (px < 0 || px > HeltecV3::Oled::WIDTH - 6) return;
        if (py < 0 || py > HeltecV3::Oled::HEIGHT - 8) return;
        int page = py / 8;
        int base = (unsigned char)c * 5;
        for (int i = 0; i < 5; ++i) {
            g_fb[page * HeltecV3::Oled::WIDTH + px + i] = oled_font5x7[base + i];
        }
        g_fb[page * HeltecV3::Oled::WIDTH + px + 5] = 0;  /* 1-px gap */
    }
}

namespace HeltecV3::Oled {

void clear() { memset(g_fb, 0, sizeof(g_fb)); }

void print(int row, int col, const char* text) {
    if (row < 0 || row >= ROWS) return;
    int y = row * 8;
    int x = col * 6;
    while (*text && x <= WIDTH - 6) {
        draw_char(x, y, *text++);
        x += 6;
    }
}

void set_line(int row, const char* text) {
    if (row < 0 || row >= ROWS) return;
    memset(g_fb + row * WIDTH, 0, WIDTH);
    print(row, 0, text);
}

void hline(int y) {
    if (y < 0 || y >= HEIGHT) return;
    int page = y / 8;
    uint8_t mask = 1 << (y % 8);
    for (int x = 0; x < WIDTH; ++x) g_fb[page * WIDTH + x] |= mask;
}

bool init() {
    if (g_ready) return true;

    /* Enable Vext — Heltec V3 routes OLED power through GPIO36 active LOW. */
    gpio_config_t vext_cfg = {};
    vext_cfg.pin_bit_mask = (1ULL << 36);
    vext_cfg.mode         = GPIO_MODE_OUTPUT;
    vext_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    vext_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    vext_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&vext_cfg);
    gpio_set_level(GPIO_NUM_36, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Reset the OLED. */
    gpio_config_t rst_cfg = {};
    rst_cfg.pin_bit_mask = (1ULL << HELTEC_V3_OLED_RST);
    rst_cfg.mode         = GPIO_MODE_OUTPUT;
    rst_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    rst_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&rst_cfg);
    gpio_set_level((gpio_num_t)HELTEC_V3_OLED_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)HELTEC_V3_OLED_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)HELTEC_V3_OLED_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port                     = I2C_NUM_0;
    bus_cfg.sda_io_num                   = (gpio_num_t)HELTEC_V3_OLED_SDA;
    bus_cfg.scl_io_num                   = (gpio_num_t)HELTEC_V3_OLED_SCL;
    bus_cfg.clk_source                   = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt            = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus_cfg, &g_bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed");
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = SSD1306_ADDR;
    dev_cfg.scl_speed_hz    = 400000;
    if (i2c_master_bus_add_device(g_bus, &dev_cfg, &g_dev) != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed");
        return false;
    }

    static const uint8_t init_seq[] = {
        0xAE,        /* display off */
        0xD5, 0x80,  /* clock divide / osc freq */
        0xA8, 0x3F,  /* mux ratio 1/64 */
        0xD3, 0x00,  /* display offset 0 */
        0x40,        /* start line 0 */
        0x8D, 0x14,  /* charge pump on */
        0x20, 0x00,  /* horizontal addressing mode */
        0xA1,        /* segment remap */
        0xC8,        /* COM scan remap */
        0xDA, 0x12,  /* COM pins hw config */
        0x81, 0xCF,  /* contrast */
        0xD9, 0xF1,  /* pre-charge */
        0xDB, 0x40,  /* VCOMH deselect */
        0xA4,        /* RAM-driven output */
        0xA6,        /* non-inverted */
        0x2E,        /* scroll off */
        0xAF,        /* display on */
    };
    for (uint8_t c : init_seq) {
        if (!i2c_cmd(c)) {
            ESP_LOGE(TAG, "SSD1306 init cmd 0x%02x failed", c);
            return false;
        }
    }

    clear();
    flush();
    g_ready = true;
    ESP_LOGI(TAG, "SSD1306 initialised");
    return true;
}

void flush() {
    if (!g_ready || g_suspended) return;
    i2c_cmd(0x21); i2c_cmd(0x00); i2c_cmd(0x7F);
    i2c_cmd(0x22); i2c_cmd(0x00); i2c_cmd(0x07);
    for (int page = 0; page < 8; ++page) {
        i2c_data_row(g_fb + page * WIDTH, WIDTH);
    }
}

void suspend() {
    if (!g_ready || g_suspended) return;
    i2c_cmd(0xAE);  /* display off — panel dark, GDDRAM preserved */
    g_suspended = true;
}

void resume() {
    if (!g_ready || !g_suspended) return;
    /* Re-push the full framebuffer in case the GDDRAM was corrupted by
     * I2C clock glitches during light sleep. Then re-enable the display. */
    g_suspended = false;
    flush();
    i2c_cmd(0xAF);  /* display on */
}

bool is_suspended() { return g_suspended; }

}
