# ESP-IDF component wrapping the uReticulum library.
# Pulls source files from ../../../../ (the repo root) and adds the ESP-IDF
# HAL backend. Vendored monocypher source is built in-place; mbedtls comes
# from ESP-IDF's bundled component.

set(URETICULUM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")

idf_component_register(
    SRCS
        "${URETICULUM_ROOT}/src/log.cpp"
        "${URETICULUM_ROOT}/src/os.cpp"
        "${URETICULUM_ROOT}/src/crc.cpp"
        "${URETICULUM_ROOT}/src/memory.cpp"
        "${URETICULUM_ROOT}/src/bytes.cpp"
        "${URETICULUM_ROOT}/src/cryptography/hashes.cpp"
        "${URETICULUM_ROOT}/src/cryptography/hmac.cpp"
        "${URETICULUM_ROOT}/src/cryptography/hkdf.cpp"
        "${URETICULUM_ROOT}/src/cryptography/aes.cpp"
        "${URETICULUM_ROOT}/src/cryptography/ed25519.cpp"
        "${URETICULUM_ROOT}/src/cryptography/x25519.cpp"
        "${URETICULUM_ROOT}/src/cryptography/fernet.cpp"
        "${URETICULUM_ROOT}/src/cryptography/token.cpp"
        "${URETICULUM_ROOT}/src/identity.cpp"
        "${URETICULUM_ROOT}/src/destination.cpp"
        "${URETICULUM_ROOT}/src/packet.cpp"
        "${URETICULUM_ROOT}/src/interface.cpp"
        "${URETICULUM_ROOT}/src/transport.cpp"
        "${URETICULUM_ROOT}/src/link.cpp"
        "${URETICULUM_ROOT}/src/resource.cpp"
        "${URETICULUM_ROOT}/src/reticulum.cpp"
        "${URETICULUM_ROOT}/src/filesystem.cpp"
        "${URETICULUM_ROOT}/src/interfaces/loopback.cpp"
        "${URETICULUM_ROOT}/third_party/tlsf/tlsf.c"
        "${URETICULUM_ROOT}/third_party/monocypher/monocypher.c"
        "${URETICULUM_ROOT}/third_party/monocypher/monocypher-ed25519.c"
        "${URETICULUM_ROOT}/port/esp_idf/hal_esp_idf.c"
    INCLUDE_DIRS
        "${URETICULUM_ROOT}/include"
        "${URETICULUM_ROOT}/third_party/tlsf"
        "${URETICULUM_ROOT}/third_party/monocypher"
    REQUIRES
        freertos
        esp_timer
        esp_hw_support
        log
        mbedtls
)

# uReticulum core uses C++ exceptions and RTTI in a few places (Identity
# error paths, doctest test build). The library itself doesn't strictly
# need exceptions on the embedded target, but enabling them avoids a
# rewrite of every throw site for now.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fexceptions
    -Wall
    -Wno-unused-parameter
)

# Force C++17 for the component
set_source_files_properties(
    "${URETICULUM_ROOT}/src/log.cpp"
    "${URETICULUM_ROOT}/src/os.cpp"
    "${URETICULUM_ROOT}/src/bytes.cpp"
    "${URETICULUM_ROOT}/src/cryptography/hashes.cpp"
    "${URETICULUM_ROOT}/src/cryptography/hmac.cpp"
    "${URETICULUM_ROOT}/src/cryptography/hkdf.cpp"
    "${URETICULUM_ROOT}/src/cryptography/aes.cpp"
    "${URETICULUM_ROOT}/src/cryptography/ed25519.cpp"
    "${URETICULUM_ROOT}/src/cryptography/x25519.cpp"
    "${URETICULUM_ROOT}/src/cryptography/fernet.cpp"
    "${URETICULUM_ROOT}/src/cryptography/token.cpp"
    "${URETICULUM_ROOT}/src/identity.cpp"
    "${URETICULUM_ROOT}/src/destination.cpp"
    "${URETICULUM_ROOT}/src/packet.cpp"
    "${URETICULUM_ROOT}/src/interface.cpp"
    "${URETICULUM_ROOT}/src/transport.cpp"
    "${URETICULUM_ROOT}/src/link.cpp"
    "${URETICULUM_ROOT}/src/resource.cpp"
    "${URETICULUM_ROOT}/src/reticulum.cpp"
    "${URETICULUM_ROOT}/src/memory.cpp"
    "${URETICULUM_ROOT}/src/crc.cpp"
    "${URETICULUM_ROOT}/src/filesystem.cpp"
    "${URETICULUM_ROOT}/src/interfaces/loopback.cpp"
    PROPERTIES COMPILE_FLAGS "-std=gnu++17"
)
