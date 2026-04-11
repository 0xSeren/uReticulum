cmake_minimum_required(VERSION 3.16)
project(uReticulum LANGUAGES C CXX)

set(URETICULUM_PORT "posix" CACHE STRING "HAL backend: posix | freertos")
set_property(CACHE URETICULUM_PORT PROPERTY STRINGS posix freertos)

option(URETICULUM_BUILD_EXAMPLES "Build smoke example(s)" ON)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(MbedTLS QUIET)
if(NOT MbedTLS_FOUND)
    find_library(MBEDTLS_CRYPTO_LIB NAMES mbedcrypto REQUIRED)
    find_library(MBEDTLS_X509_LIB   NAMES mbedx509)
    find_library(MBEDTLS_TLS_LIB    NAMES mbedtls)
    find_path(MBEDTLS_INCLUDE_DIR mbedtls/sha256.h REQUIRED)
endif()

find_library(MONOCYPHER_LIB NAMES monocypher REQUIRED)
find_path(MONOCYPHER_INCLUDE_DIR monocypher.h REQUIRED)

add_library(ureticulum STATIC
    src/log.cpp
    src/os.cpp
    src/crc.cpp
    src/memory.cpp
    src/bytes.cpp
    src/cryptography/hashes.cpp
    src/cryptography/hmac.cpp
    src/cryptography/hkdf.cpp
    src/cryptography/aes.cpp
    src/cryptography/ed25519.cpp
    src/cryptography/x25519.cpp
    src/cryptography/fernet.cpp
    src/cryptography/token.cpp
    src/identity.cpp
    src/destination.cpp
    src/packet.cpp
    src/interface.cpp
    src/transport.cpp
    src/reticulum.cpp
    src/interfaces/loopback.cpp
    third_party/tlsf/tlsf.c
)

target_include_directories(ureticulum
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tlsf
)

if(MbedTLS_FOUND)
    target_link_libraries(ureticulum PUBLIC MbedTLS::mbedcrypto)
else()
    target_include_directories(ureticulum PUBLIC ${MBEDTLS_INCLUDE_DIR})
    target_link_libraries(ureticulum PUBLIC ${MBEDTLS_CRYPTO_LIB})
endif()

target_include_directories(ureticulum PUBLIC ${MONOCYPHER_INCLUDE_DIR})
target_link_libraries(ureticulum PUBLIC ${MONOCYPHER_LIB})

target_compile_options(ureticulum PRIVATE
    -Wall -Wextra -Wno-unused-parameter
)

if(URETICULUM_PORT STREQUAL "posix")
    add_library(ureticulum_hal STATIC port/posix/hal_posix.c)
    target_include_directories(ureticulum_hal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    find_package(Threads REQUIRED)
    target_link_libraries(ureticulum_hal PUBLIC Threads::Threads)
    target_link_libraries(ureticulum PUBLIC ureticulum_hal)

elseif(URETICULUM_PORT STREQUAL "freertos")
    if(NOT TARGET freertos_kernel)
        message(WARNING
            "URETICULUM_PORT=freertos but target 'freertos_kernel' is not defined. "
            "Link your firmware against a FreeRTOS kernel target providing "
            "FreeRTOS.h, task.h, semphr.h.")
    endif()
    add_library(ureticulum_hal STATIC port/freertos/hal_freertos.c)
    target_include_directories(ureticulum_hal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(TARGET freertos_kernel)
        target_link_libraries(ureticulum_hal PUBLIC freertos_kernel)
    endif()
    target_link_libraries(ureticulum PUBLIC ureticulum_hal)

else()
    message(FATAL_ERROR "Unknown URETICULUM_PORT='${URETICULUM_PORT}' (expected posix|freertos)")
endif()

if(URETICULUM_BUILD_EXAMPLES AND URETICULUM_PORT STREQUAL "posix")
    add_executable(ureticulum_smoke examples/smoke/main.cpp)
    target_link_libraries(ureticulum_smoke PRIVATE ureticulum)
endif()

option(URETICULUM_BUILD_TESTS "Build host unit tests with doctest" ON)

if(URETICULUM_BUILD_TESTS AND URETICULUM_PORT STREQUAL "posix")
    add_executable(ureticulum_tests
        test/test_main.cpp
        test/test_crc.cpp
        test/test_bytes.cpp
        test/test_pkcs7.cpp
        test/test_tlsf.cpp
        test/test_crypto.cpp
        test/test_identity.cpp
        test/test_loopback.cpp
        test/test_transport.cpp
    )
    target_include_directories(ureticulum_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest
    )
    target_link_libraries(ureticulum_tests PRIVATE ureticulum)
    enable_testing()
    add_test(NAME ureticulum_tests COMMAND ureticulum_tests)
endif()
