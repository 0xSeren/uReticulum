#pragma once

#include <RadioLib.h>

/* RadioLibHal subclass that maps RadioLib's hardware abstraction onto the
 * ESP-IDF SPI/GPIO/timing primitives. Lives in the firmware project (not
 * the uReticulum core) because the SPI/GPIO surface is firmware-specific
 * and the Reticulum core doesn't need to know about it. */
class EspIdfHal : public RadioLibHal {
public:
    EspIdfHal(int sck, int miso, int mosi, int nss, int spi_host = 1);

    void   init() override;
    void   term() override;

    void     pinMode(uint32_t pin, uint32_t mode) override;
    void     digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void     attachInterrupt(uint32_t interrupt_num, void (*intp)(void), uint32_t mode) override;
    void     detachInterrupt(uint32_t interrupt_num) override;

    void     delay(RadioLibTime_t ms) override;
    void     delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long     pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;

    void     spiBegin() override;
    void     spiBeginTransaction() override;
    void     spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void     spiEndTransaction() override;
    void     spiEnd() override;

private:
    int  _sck;
    int  _miso;
    int  _mosi;
    int  _nss;
    int  _spi_host;
    void* _spi_device;  /* spi_device_handle_t opaque */
};
