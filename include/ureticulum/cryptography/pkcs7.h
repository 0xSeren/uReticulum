#pragma once

#include <stdexcept>
#include <string>

#include "ureticulum/bytes.h"

namespace RNS { namespace Cryptography {

    class PKCS7 {
    public:
        static const size_t BLOCKSIZE = 16;

        static inline Bytes pad(const Bytes& data, size_t bs = BLOCKSIZE) {
            Bytes padded(data);
            inplace_pad(padded, bs);
            return padded;
        }

        static inline Bytes unpad(const Bytes& data, size_t bs = BLOCKSIZE) {
            Bytes unpadded(data);
            inplace_unpad(unpadded, bs);
            return unpadded;
        }

        static inline void inplace_pad(Bytes& data, size_t bs = BLOCKSIZE) {
            size_t  padlen = bs - (data.size() % bs);
            uint8_t pad[bs];
            memset(pad, (int)padlen, padlen);
            data.append(pad, padlen);
        }

        static inline void inplace_unpad(Bytes& data, size_t bs = BLOCKSIZE) {
            size_t len = data.size();
            if (len == 0) throw std::runtime_error("Cannot unpad empty data");
            size_t padlen = (size_t)data.data()[len - 1];
            if (padlen == 0 || padlen > bs)
                throw std::runtime_error("Cannot unpad, invalid padding length of " + std::to_string(padlen) + " bytes");
            data.resize(len - padlen);
        }
    };

}}
