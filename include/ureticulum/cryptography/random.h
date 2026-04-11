#pragma once

#include <stdint.h>
#include <stdexcept>

#include "ureticulum/bytes.h"
#include "ureticulum/hal.h"

namespace RNS { namespace Cryptography {

    inline Bytes random(size_t length) {
        Bytes rand;
        uint8_t* buf = rand.writable(length);
        if (ur_hal_random_bytes(buf, length) != 0)
            throw std::runtime_error("ur_hal_random_bytes failed");
        return rand;
    }

    inline uint32_t randomnum() {
        uint8_t b[4];
        if (ur_hal_random_bytes(b, 4) != 0)
            throw std::runtime_error("ur_hal_random_bytes failed");
        return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
               ((uint32_t)b[2] <<  8) | ((uint32_t)b[3]);
    }

    inline uint32_t randomnum(uint32_t max) { return randomnum() % max; }

    inline float random_float() {
        return (float)randomnum() / (float)0xffffffffU;
    }

}}
