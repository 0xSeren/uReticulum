#pragma once

#include <stddef.h>

#include "ureticulum/bytes.h"

namespace RNS { namespace Cryptography {

    Bytes hkdf(size_t length,
               const Bytes& derive_from,
               const Bytes& salt    = {Bytes::NONE},
               const Bytes& context = {Bytes::NONE});

}}
