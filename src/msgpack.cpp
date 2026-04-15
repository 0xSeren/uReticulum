#include "ureticulum/msgpack.h"
#include <string.h>

namespace RNS::MsgPack {

void pack_nil(Bytes& out)          { out.append((uint8_t)0xC0); }
void pack_bool(Bytes& out, bool v) { out.append((uint8_t)(v ? 0xC3 : 0xC2)); }

void pack_int(Bytes& out, int64_t v) {
    if (v >= 0 && v <= 127) {
        out.append((uint8_t)v);
    } else if (v >= 0 && v <= 0xFF) {
        out.append((uint8_t)0xCC); out.append((uint8_t)v);
    } else if (v >= 0 && v <= 0xFFFF) {
        out.append((uint8_t)0xCD);
        out.append((uint8_t)((v >> 8) & 0xFF));
        out.append((uint8_t)(v & 0xFF));
    } else if (v >= 0 && v <= 0xFFFFFFFF) {
        out.append((uint8_t)0xCE);
        out.append((uint8_t)((v >> 24) & 0xFF));
        out.append((uint8_t)((v >> 16) & 0xFF));
        out.append((uint8_t)((v >>  8) & 0xFF));
        out.append((uint8_t)( v        & 0xFF));
    } else if (v >= -32 && v < 0) {
        out.append((uint8_t)(v & 0xFF));
    } else if (v >= -128 && v < 0) {
        out.append((uint8_t)0xD0); out.append((uint8_t)(v & 0xFF));
    } else {
        out.append((uint8_t)0xD3);
        for (int i = 7; i >= 0; --i) out.append((uint8_t)((v >> (i * 8)) & 0xFF));
    }
}

void pack_float64(Bytes& out, double v) {
    out.append((uint8_t)0xCB);
    uint64_t bits;
    memcpy(&bits, &v, 8);
    for (int i = 7; i >= 0; --i) out.append((uint8_t)((bits >> (i * 8)) & 0xFF));
}

void pack_bin(Bytes& out, const uint8_t* data, size_t len) {
    if (len <= 0xFF) {
        out.append((uint8_t)0xC4); out.append((uint8_t)len);
    } else if (len <= 0xFFFF) {
        out.append((uint8_t)0xC5);
        out.append((uint8_t)((len >> 8) & 0xFF));
        out.append((uint8_t)(len & 0xFF));
    } else {
        out.append((uint8_t)0xC6);
        out.append((uint8_t)((len >> 24) & 0xFF));
        out.append((uint8_t)((len >> 16) & 0xFF));
        out.append((uint8_t)((len >>  8) & 0xFF));
        out.append((uint8_t)( len        & 0xFF));
    }
    for (size_t i = 0; i < len; ++i) out.append(data[i]);
}

void pack_bin(Bytes& out, const Bytes& b) { pack_bin(out, b.data(), b.size()); }

void pack_array_header(Bytes& out, size_t count) {
    if (count <= 15) {
        out.append((uint8_t)(0x90 | count));
    } else if (count <= 0xFFFF) {
        out.append((uint8_t)0xDC);
        out.append((uint8_t)((count >> 8) & 0xFF));
        out.append((uint8_t)(count & 0xFF));
    } else {
        out.append((uint8_t)0xDD);
        out.append((uint8_t)((count >> 24) & 0xFF));
        out.append((uint8_t)((count >> 16) & 0xFF));
        out.append((uint8_t)((count >>  8) & 0xFF));
        out.append((uint8_t)( count        & 0xFF));
    }
}

/* --- Reader --- */

bool Reader::read_nil() {
    if (peek() == 0xC0) { next(); return true; }
    return false;
}

bool Reader::read_bool() {
    uint8_t b = next();
    return b == 0xC3;
}

int64_t Reader::read_int() {
    uint8_t b = next();
    if (b <= 0x7F) return (int64_t)b;
    if (b >= 0xE0) return (int64_t)(int8_t)b;
    switch (b) {
        case 0xCC: return (int64_t)next();
        case 0xCD: { uint16_t v = (uint16_t)next() << 8; v |= next(); return v; }
        case 0xCE: { uint32_t v = 0; for (int i=0;i<4;i++) v = (v<<8)|next(); return v; }
        case 0xCF: { uint64_t v = 0; for (int i=0;i<8;i++) v = (v<<8)|next(); return (int64_t)v; }
        case 0xD0: return (int64_t)(int8_t)next();
        case 0xD1: { int16_t v = (int16_t)((uint16_t)next()<<8); v|=next(); return v; }
        case 0xD2: { int32_t v = 0; for (int i=0;i<4;i++) v = (v<<8)|next(); return v; }
        case 0xD3: { int64_t v = 0; for (int i=0;i<8;i++) v = (v<<8)|(uint64_t)next(); return v; }
    }
    return 0;
}

double Reader::read_float64() {
    uint8_t b = next();
    if (b == 0xCA) {
        uint32_t bits = 0;
        for (int i=0;i<4;i++) bits = (bits<<8)|next();
        float f; memcpy(&f, &bits, 4); return (double)f;
    }
    /* 0xCB = float64 */
    uint64_t bits = 0;
    for (int i=0;i<8;i++) bits = (bits<<8)|(uint64_t)next();
    double d; memcpy(&d, &bits, 8); return d;
}

Bytes Reader::read_bin() {
    uint8_t b = next();
    size_t len = 0;
    switch (b) {
        case 0xC4: len = next(); break;
        case 0xC5: len = (size_t)next()<<8; len |= next(); break;
        case 0xC6: for (int i=0;i<4;i++) len = (len<<8)|next(); break;
        /* Also handle fixstr (0xA0-0xBF) and str8/16/32 since Python
         * sometimes sends strings where we expect bin. */
        default:
            if ((b & 0xE0) == 0xA0) { len = b & 0x1F; break; }
            if (b == 0xD9) { len = next(); break; }
            if (b == 0xDA) { len = (size_t)next()<<8; len |= next(); break; }
            if (b == 0xDB) { for (int i=0;i<4;i++) len = (len<<8)|next(); break; }
            return Bytes{Bytes::NONE};
    }
    if (_pos + len > _data.size()) return Bytes{Bytes::NONE};
    Bytes result(_data.data() + _pos, len);
    _pos += len;
    return result;
}

size_t Reader::read_array_header() {
    uint8_t b = next();
    if ((b & 0xF0) == 0x90) return b & 0x0F;
    if (b == 0xDC) { size_t n = (size_t)next()<<8; n |= next(); return n; }
    if (b == 0xDD) { size_t n = 0; for (int i=0;i<4;i++) n=(n<<8)|next(); return n; }
    return 0;
}

void Reader::skip() {
    uint8_t b = peek();
    if (b == 0xC0 || b == 0xC2 || b == 0xC3) { next(); return; }
    if (b <= 0x7F || b >= 0xE0) { next(); return; }
    if ((b & 0xE0) == 0xA0) { read_bin(); return; }
    if (b >= 0xCC && b <= 0xD3) { read_int(); return; }
    if (b == 0xCA || b == 0xCB) { read_float64(); return; }
    if (b >= 0xC4 && b <= 0xC6) { read_bin(); return; }
    if (b == 0xD9 || b == 0xDA || b == 0xDB) { read_bin(); return; }
    if ((b & 0xF0) == 0x90 || b == 0xDC || b == 0xDD) {
        size_t n = read_array_header();
        for (size_t i = 0; i < n; ++i) skip();
        return;
    }
    next(); /* unknown — consume one byte */
}

}
