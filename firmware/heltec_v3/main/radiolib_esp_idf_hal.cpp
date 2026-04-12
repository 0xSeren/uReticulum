#include "radiolib_esp_idf_hal.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

EspIdfHal::EspIdfHal(int sck, int miso, int mosi, int nss, int spi_host)
    : RadioLibHal(GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, 0, 1, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE),
      _sck(sck), _miso(miso), _mosi(mosi), _nss(nss),
      _spi_host(spi_host), _spi_device(nullptr) {}

void EspIdfHal::init() {
    spiBegin();
}

void EspIdfHal::term() {
    spiEnd();
}

void EspIdfHal::pinMode(uint32_t pin, uint32_t mode) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode         = (mode == GPIO_MODE_OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void EspIdfHal::digitalWrite(uint32_t pin, uint32_t value) {
    gpio_set_level((gpio_num_t)pin, value);
}

uint32_t EspIdfHal::digitalRead(uint32_t pin) {
    return gpio_get_level((gpio_num_t)pin);
}

void EspIdfHal::attachInterrupt(uint32_t interrupt_num, void (*intp)(void), uint32_t mode) {
    /* RadioLib only uses one interrupt — DIO1 on SX126x. We assume the
     * caller has already wired the GPIO interrupt service. */
    gpio_install_isr_service(0);
    gpio_set_intr_type((gpio_num_t)interrupt_num, (gpio_int_type_t)mode);
    gpio_isr_handler_add((gpio_num_t)interrupt_num, (gpio_isr_t)intp, nullptr);
}

void EspIdfHal::detachInterrupt(uint32_t interrupt_num) {
    gpio_isr_handler_remove((gpio_num_t)interrupt_num);
}

void EspIdfHal::delay(RadioLibTime_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void EspIdfHal::delayMicroseconds(RadioLibTime_t us) {
    /* For sub-millisecond delays we busy-wait against esp_timer. */
    int64_t start = esp_timer_get_time();
    while (esp_timer_get_time() - start < (int64_t)us) { /* spin */ }
}

RadioLibTime_t EspIdfHal::millis() { return esp_timer_get_time() / 1000ULL; }
RadioLibTime_t EspIdfHal::micros() { return esp_timer_get_time(); }

long EspIdfHal::pulseIn(uint32_t /*pin*/, uint32_t /*state*/, RadioLibTime_t /*timeout*/) {
    /* Not used by SX126x. Stub. */
    return 0;
}

void EspIdfHal::spiBegin() {
    if (_spi_device) return;

    spi_bus_config_t bus = {};
    bus.miso_io_num     = _miso;
    bus.mosi_io_num     = _mosi;
    bus.sclk_io_num     = _sck;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = 4096;
    spi_bus_initialize((spi_host_device_t)_spi_host, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = 8 * 1000 * 1000;  /* 8 MHz — well within SX1262 limits */
    dev.mode           = 0;
    dev.spics_io_num   = _nss;
    dev.queue_size     = 1;
    spi_device_handle_t handle;
    spi_bus_add_device((spi_host_device_t)_spi_host, &dev, &handle);
    _spi_device = handle;
}

void EspIdfHal::spiBeginTransaction() { /* device handle owns CS, nothing to do */ }
void EspIdfHal::spiEndTransaction()   {}

void EspIdfHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = out;
    t.rx_buffer = in;
    spi_device_transmit((spi_device_handle_t)_spi_device, &t);
}

void EspIdfHal::spiEnd() {
    if (_spi_device) {
        spi_bus_remove_device((spi_device_handle_t)_spi_device);
        _spi_device = nullptr;
        spi_bus_free((spi_host_device_t)_spi_host);
    }
}
