#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include <vector>
#include <string>
#include <memory>
#include <stdexcept>

#include "ureticulum/log.h"
#include "ureticulum/memory.h"

inline void* memmem(const void* haystack, size_t haystack_len, const void* needle, size_t needle_len) {
    const unsigned char* h = (const unsigned char*)haystack;
    const unsigned char* n = (const unsigned char*)needle;

    if (needle_len == 0) return (void*)h;
    if (haystack_len < needle_len) return nullptr;

    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (h[i] == n[0] && memcmp(&h[i], n, needle_len) == 0) {
            return (void*)&h[i];
        }
    }
    return nullptr;
}

namespace RNS {

    #define COW

    class Bytes {
    private:
        using Data       = std::vector<uint8_t>;
        using SharedData = std::shared_ptr<Data>;

    public:
        enum NoneConstructor { NONE };

        Bytes() = default;
        Bytes(const NoneConstructor) {}
        Bytes(const Bytes& bytes) { assign(bytes); }
        Bytes(const Data& data) { assign(data); }
        Bytes(Data&& rdata) { assign(std::move(rdata)); }
        Bytes(const uint8_t* chunk, size_t size) { assign(chunk, size); }
        Bytes(const void* chunk, size_t size) { assign(chunk, size); }
        Bytes(const char* string) { assign(string); }
        Bytes(const std::string& string) { assign(string); }
        Bytes(size_t capacity) { newData(capacity); }
        virtual ~Bytes() = default;

        inline const Bytes& operator = (const Bytes& bytes) { assign(bytes); return *this; }
        inline const Bytes& operator += (const Bytes& bytes) { append(bytes); return *this; }
        inline const Bytes& operator += (const Data& data) { append(data); return *this; }

        inline Bytes operator + (const Bytes& bytes) const {
            Bytes nb(*this);
            nb.append(bytes);
            return nb;
        }
        inline bool operator == (const Bytes& bytes) const { return compare(bytes) == 0; }
        inline bool operator != (const Bytes& bytes) const { return compare(bytes) != 0; }
        inline bool operator <  (const Bytes& bytes) const { return compare(bytes) <  0; }
        inline bool operator >  (const Bytes& bytes) const { return compare(bytes) >  0; }

        inline uint8_t& operator[](size_t index) {
            if (!_data || index >= _data->size()) throw std::out_of_range("Index out of bounds");
            /* Non-const access must own its data — otherwise a write here would
             * mutate every COW-shared instance. */
            exclusiveData(true);
            return (*_data)[index];
        }
        inline const uint8_t& operator[](size_t index) const {
            if (!_data || index >= _data->size()) throw std::out_of_range("Index out of bounds");
            return (*_data)[index];
        }

        inline operator bool() const { return (_data && !_data->empty()); }
        inline operator const Data() const { return _data ? *_data : Data(); }

    private:
        inline SharedData shareData() const { _exclusive = false; return _data; }
        void newData(size_t capacity = 0);
        void exclusiveData(bool copy = true, size_t capacity = 0);

    public:
        inline void clear() { _data = nullptr; _exclusive = true; }

        inline void assign(const Bytes& bytes) {
#ifdef COW
            _data = bytes.shareData();
            _exclusive = false;
#else
            if (bytes.size() == 0) { _data = nullptr; _exclusive = true; return; }
            exclusiveData(false, bytes._data->size());
            *_data = *bytes._data;
#endif
        }
        inline void assign(const Data& data) {
            if (data.empty()) { _data = nullptr; _exclusive = true; return; }
            exclusiveData(false, data.size());
            *_data = data;
        }
        inline void assign(Data&& rdata) {
            if (rdata.empty()) { _data = nullptr; _exclusive = true; return; }
            exclusiveData(false);
            *_data = std::move(rdata);
        }
        inline void assign(const uint8_t* chunk, size_t chunk_size) {
            if (chunk == nullptr || chunk_size == 0) { _data = nullptr; _exclusive = true; return; }
            exclusiveData(false, chunk_size);
            _data->assign(chunk, chunk + chunk_size);
        }
        inline void assign(const void* chunk, size_t chunk_size) { assign((const uint8_t*)chunk, chunk_size); }
        inline void assign(const char* string) {
            if (string == nullptr || string[0] == 0) { _data = nullptr; _exclusive = true; return; }
            size_t n = strlen(string);
            exclusiveData(false, n);
            _data->assign((const uint8_t*)string, (const uint8_t*)string + n);
        }
        inline void assign(const std::string& string) { assign((const uint8_t*)string.c_str(), string.length()); }

        void assignHex(const uint8_t* hex, size_t hex_size);
        inline void assignHex(const char* hex) { if (!hex) return; assignHex((const uint8_t*)hex, strlen(hex)); }

        inline void append(const Bytes& bytes) {
            if (bytes.size() == 0) return;
            exclusiveData(true, size() + bytes.size());
            _data->insert(_data->end(), bytes._data->begin(), bytes._data->end());
        }
        inline void append(const Data& data) {
            if (data.empty()) return;
            exclusiveData(true, size() + data.size());
            _data->insert(_data->end(), data.begin(), data.end());
        }
        inline void append(const uint8_t* chunk, size_t chunk_size) {
            if (chunk == nullptr || chunk_size == 0) return;
            exclusiveData(true, size() + chunk_size);
            _data->insert(_data->end(), chunk, chunk + chunk_size);
        }
        inline void append(const void* chunk, size_t chunk_size) { append((const uint8_t*)chunk, chunk_size); }
        inline void append(const char* string) {
            if (string == nullptr || string[0] == 0) return;
            size_t n = strlen(string);
            exclusiveData(true, size() + n);
            _data->insert(_data->end(), (const uint8_t*)string, (const uint8_t*)string + n);
        }
        inline void append(uint8_t byte) {
            exclusiveData(true, size() + 1);
            _data->push_back(byte);
        }
        inline void append(const std::string& string) { append((const uint8_t*)string.c_str(), string.length()); }

        void appendHex(const uint8_t* hex, size_t hex_size);
        inline void appendHex(const char* hex) { if (!hex) return; appendHex((const uint8_t*)hex, strlen(hex)); }

        inline uint8_t* writable(size_t size) {
            if (size > 0) {
                exclusiveData(false, size);
                resize(size);
                return _data->data();
            }
            else if (_data) {
                size_t current = _data->size();
                exclusiveData(false, current);
                resize(current);
                return _data->data();
            }
            return nullptr;
        }

        inline void resize(size_t newsize) {
            if (newsize == size()) return;
            exclusiveData(true);
            _data->resize(newsize);
        }

        int compare(const Bytes& bytes) const;
        int compare(const uint8_t* buf, size_t size) const;
        inline int compare(const char* str) const {
            if (!str) return empty() ? 0 : 1;
            return compare((const uint8_t*)str, strlen(str));
        }

        inline size_t   size()     const { return _data ? _data->size() : 0; }
        inline bool     empty()    const { return !_data || _data->empty(); }
        inline size_t   capacity() const { return _data ? _data->capacity() : 0; }
        inline void     reserve(size_t cap) const { if (_data) _data->reserve(cap); }
        inline const uint8_t* data() const { return _data ? _data->data() : nullptr; }
        inline const Data collection() const { return _data ? *_data : Data(); }

        inline std::string toString() const { return _data ? std::string{(const char*)data(), size()} : std::string{}; }
        std::string toHex(bool upper = false) const;
        Bytes mid(size_t beginpos, size_t len) const;
        Bytes mid(size_t beginpos) const;
        inline Bytes left(size_t len) const {
            if (!_data) return NONE;
            if (len > size()) len = size();
            return {data(), len};
        }
        inline Bytes right(size_t len) const {
            if (!_data) return NONE;
            if (len > size()) len = size();
            return {data() + (size() - len), len};
        }
        inline int find(int pos, const char* str) {
            if (!str || !_data || _data->data() == nullptr || (size_t)pos >= _data->size()) return -1;
            void* ptr = memmem((const void*)(_data->data() + pos), (_data->size() - pos), (const void*)str, strlen(str));
            if (ptr == nullptr) return -1;
            return (int)((uint8_t*)ptr - _data->data());
        }
        inline int find(const char* str) { return find(0, str); }

    private:
        SharedData _data;
        mutable bool _exclusive = true;
    };

    inline Bytes bytesFromChunk(const uint8_t* ptr, size_t len) { return {ptr, len}; }
    inline Bytes bytesFromString(const char* str) { return str ? Bytes{(const uint8_t*)str, strlen(str)} : Bytes{}; }
    inline std::string stringFromBytes(const Bytes& bytes) { return bytes.toString(); }
    inline std::string hexFromBytes(const Bytes& bytes)    { return bytes.toHex(); }
    std::string hexFromByte(uint8_t byte, bool upper = true);

}

inline RNS::Bytes& operator << (RNS::Bytes& lh, const RNS::Bytes& rh) { lh.append(rh); return lh; }
inline RNS::Bytes& operator << (RNS::Bytes& lh, uint8_t rh)           { lh.append(rh); return lh; }
inline RNS::Bytes& operator << (RNS::Bytes& lh, const char* rh)       { lh.append(rh); return lh; }
