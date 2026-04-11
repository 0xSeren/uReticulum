#include "ureticulum/bytes.h"

namespace RNS {

void Bytes::newData(size_t capacity) {
    Data* data = new Data();
    if (capacity > 0) data->reserve(capacity);
    _data = SharedData(data);
    _exclusive = true;
}

void Bytes::exclusiveData(bool copy, size_t capacity) {
    if (!_data) {
        newData(capacity);
    }
    else if (!_exclusive) {
        if (copy && !_data->empty()) {
            Data* data = new Data();
            data->reserve((capacity > _data->size()) ? capacity : _data->size());
            data->insert(data->begin(), _data->begin(), _data->end());
            _data = SharedData(data);
            _exclusive = true;
        }
        else {
            newData(capacity);
        }
    }
    else if (capacity > 0 && capacity > size()) {
        reserve(capacity);
    }
}

int Bytes::compare(const Bytes& bytes) const {
    /* Empty == empty regardless of whether _data is null or a zero-length vector. */
    bool a_empty = !_data || _data->empty();
    bool b_empty = !bytes._data || bytes._data->empty();
    if (a_empty && b_empty) return 0;
    if (a_empty)            return -1;
    if (b_empty)            return 1;
    if (*_data <  *bytes._data) return -1;
    if (*_data >  *bytes._data) return 1;
    return 0;
}

int Bytes::compare(const uint8_t* buf, size_t size) const {
    bool empty_self = !_data || _data->empty();
    if (empty_self && size == 0) return 0;
    if (empty_self)              return -1;
    int cmp = memcmp(_data->data(), buf, (_data->size() < size) ? _data->size() : size);
    if (cmp == 0 && _data->size() < size) return -1;
    if (cmp == 0 && _data->size() > size) return 1;
    return cmp;
}

void Bytes::assignHex(const uint8_t* hex, size_t hex_size) {
    if (hex == nullptr || hex_size == 0) { _data = nullptr; _exclusive = true; return; }
    hex_size &= ~(size_t)1;
    if (hex_size == 0) { _data = nullptr; _exclusive = true; return; }
    exclusiveData(false, hex_size / 2);
    _data->clear();
    for (size_t i = 0; i < hex_size; i += 2) {
        uint8_t byte = (hex[i] % 32 + 9) % 25 * 16 + (hex[i+1] % 32 + 9) % 25;
        _data->push_back(byte);
    }
}

void Bytes::appendHex(const uint8_t* hex, size_t hex_size) {
    if (hex == nullptr || hex_size == 0) return;
    hex_size &= ~(size_t)1;
    if (hex_size == 0) return;
    exclusiveData(true, size() + (hex_size / 2));
    for (size_t i = 0; i < hex_size; i += 2) {
        uint8_t byte = (hex[i] % 32 + 9) % 25 * 16 + (hex[i+1] % 32 + 9) % 25;
        _data->push_back(byte);
    }
}

static const char hex_upper_chars[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
static const char hex_lower_chars[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

std::string hexFromByte(uint8_t byte, bool upper) {
    std::string hex;
    const char* t = upper ? hex_upper_chars : hex_lower_chars;
    hex += t[(byte & 0xF0) >> 4];
    hex += t[(byte & 0x0F)];
    return hex;
}

std::string Bytes::toHex(bool upper) const {
    if (!_data) return "";
    std::string hex;
    hex.reserve(_data->size() * 2);
    const char* t = upper ? hex_upper_chars : hex_lower_chars;
    for (uint8_t byte : *_data) {
        hex += t[(byte & 0xF0) >> 4];
        hex += t[(byte & 0x0F)];
    }
    return hex;
}

Bytes Bytes::mid(size_t beginpos, size_t len) const {
    if (!_data || beginpos >= size()) return NONE;
    size_t remaining = size() - beginpos;
    if (len > remaining) len = remaining;
    return {data() + beginpos, len};
}

Bytes Bytes::mid(size_t beginpos) const {
    if (!_data || beginpos >= size()) return NONE;
    return {data() + beginpos, size() - beginpos};
}

}
