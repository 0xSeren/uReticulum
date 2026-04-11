#pragma once

#include "ureticulum/bytes.h"

namespace RNS { namespace Cryptography {

    Bytes sha256(const Bytes& data);
    Bytes sha512(const Bytes& data);

}}
