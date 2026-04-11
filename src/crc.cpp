#include "ureticulum/crc.h"

namespace RNS { namespace Utilities {

uint32_t Crc::crc32(uint32_t crc, const uint8_t* buf, size_t size) {
    if (buf == nullptr) return 0;
    crc ^= 0xffffffff;
    while (size--) {
        crc ^= *buf++;
        for (unsigned k = 0; k < 8; k++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
    }
    return crc ^ 0xffffffff;
}

}}
