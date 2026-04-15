#pragma once

#include "ureticulum/bytes.h"
#include <stdint.h>
#include <vector>

/* Minimal msgpack encoder/decoder — just enough for Reticulum's Link
 * request/response protocol. Supports: nil, bool, int, float64,
 * bin (raw bytes), arrays. Does NOT support maps, strings, ext. */

namespace RNS::MsgPack {

    /* Pack helpers — append to a Bytes buffer. */
    void pack_nil(Bytes& out);
    void pack_bool(Bytes& out, bool v);
    void pack_int(Bytes& out, int64_t v);
    void pack_float64(Bytes& out, double v);
    void pack_bin(Bytes& out, const uint8_t* data, size_t len);
    void pack_bin(Bytes& out, const Bytes& b);
    void pack_array_header(Bytes& out, size_t count);

    /* Unpack — simple sequential reader over a Bytes buffer. */
    class Reader {
    public:
        Reader(const Bytes& data) : _data(data), _pos(0) {}
        Reader(const uint8_t* data, size_t len) : _data(data, len), _pos(0) {}

        bool at_end() const { return _pos >= _data.size(); }
        size_t remaining() const { return _data.size() - _pos; }

        /* Type peek (returns the format byte without consuming). */
        uint8_t peek() const { return _pos < _data.size() ? _data.data()[_pos] : 0; }

        /* Consumers — advance past the current value. */
        bool   read_nil();           /* returns true if it was nil */
        bool   read_bool();
        int64_t read_int();
        double read_float64();
        Bytes  read_bin();
        size_t read_array_header();  /* returns element count */

        /* Skip one value of any type. */
        void skip();

    private:
        uint8_t next() { return _pos < _data.size() ? _data.data()[_pos++] : 0; }
        Bytes _data;
        size_t _pos;
    };

}
