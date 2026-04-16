# RTReticulum

*Realtime Reticulum: a native Reticulum stack for microcontrollers.*

> **WIP** - RTReticulum is under active development. The core protocol
> (Identity, Destination, Packet, Link, Transport) works and interoperates
> with upstream Python Reticulum and real RNode hardware. Missing features
> include announce rate limiting, path expiry, ratchets, IFAC, Channel,
> and Buffer. See [TODO](TODO) for the full list.

A fast, power efficient, native [Reticulum](https://reticulum.network) stack for embedded microcontrollers.

| | RNode | microReticulum | RTReticulum |
|---|---|---|---|
| Autonomous node | No (host-driven modem) | Yes | Yes |
| NomadNet pages | No | No | Yes |
| WiFi + TCP bridge | No | No | Yes |
| BLE bridge | Bluedroid | No | NimBLE |
| OTA updates | No | No | Yes |
| Sleep | No (Arduino loop) | No | 99% idle |
| Hardware crypto | No | No | mbedTLS accelerated |
| Radio driver | Arduino-LoRa | Arduino-LoRa | RadioLib (SX1262 native) |

## What works

- **Full Reticulum protocol**: Identity, Destination, Packet, Link,
  Transport with announce propagation and path discovery
- **LoRa mesh**: SX1262 via RadioLib: peer-to-peer encrypted Links over
  915 MHz with automatic announce rebroadcast
- **WiFi + TCP bridge**: connect to an upstream `rnsd` instance to bridge
  LoRa traffic to the wider Reticulum network over the Internet
- **NomadNet node**: serves a live metrics page (uptime, battery, heap,
  RSSI, interfaces, known peers) that NomadNet users can browse
- **RNode bridge**: USB serial or Bluetooth LE KISS interface: the board
  acts as an RNode that Python Reticulum can drive directly
- **All crypto in hardware**: Ed25519, X25519, AES-256-CBC, HMAC-SHA256,
  HKDF, SHA-256/512 via mbedTLS + Monocypher
- **99% CPU idle** in steady state, watchdog-clean boot

## Supported hardware

| Board | MCU | Radio | Status |
|---|---|---|---|
| Heltec WiFi LoRa 32 V3 | ESP32-S3 | SX1262 | Primary target, fully working |
| Heltec WiFi LoRa 32 V4 | ESP32-S3 | SX1262 + PA/LNA | Port in progress |
| RAK3172 (STM32WLE5CC) | Cortex-M4F + SX1262 SoC |: | Port in progress |

## Build

RTReticulum uses Nix for reproducible builds. All toolchains (ESP-IDF,
ARM GCC, host compilers, mbedTLS, Monocypher) come from `flake.nix`.

```bash
cd RTReticulum
nix develop
```

### Firmware (Heltec V3)

```bash
cd firmware/heltec_v3

# Configure (optional: defaults work out of the box)
idf.py menuconfig

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash
```

## Firmware modes

The Heltec V3 firmware supports three operating modes, selectable via
`idf.py menuconfig` under **RTReticulum Heltec V3**:

### Transport node (default)

The board runs a full Reticulum stack with its own persistent Identity.
It announces on LoRa and (if configured) bridges to the Internet via
WiFi + TCP. It hosts a NomadNet-compatible page with live system metrics.

### RNode bridge (USB UART)

The board exposes the SX1262 as an RNode over USB serial using the KISS
protocol. Python Reticulum (`rnsd`, NomadNet, Sideband) can drive it
directly with `interface_type = RNodeInterface`. The RTReticulum stack is
not started: the board is a dumb radio modem.

### RNode bridge (Bluetooth LE)

Same as the USB variant, but the KISS interface runs over a Nordic UART
Service BLE GATT profile. Connect from Python RNS with
`port = ble://RNode XXXX`. Adds NimBLE (~300 KB) to the firmware.

## Configuration

WiFi and TCP bridge settings are under `idf.py menuconfig` →
**RTReticulum WiFi**:

| Setting | Description | Default |
|---|---|---|
| `WIFI_DEFAULT_SSID` | WiFi network name | empty (WiFi disabled) |
| `WIFI_DEFAULT_PSK` | WiFi password | empty |
| `TCP_INTERFACE_HOST` | Upstream rnsd IP/hostname | empty (TCP disabled) |
| `TCP_INTERFACE_PORT` | rnsd TCP port | 4965 |
All settings can also be overridden at runtime via NVS (the firmware
checks NVS first, then falls back to menuconfig defaults).

### Identity persistence

The node's 64-byte Ed25519+X25519 private key is stored in NVS on first
boot and reused across reboots. The destination hash is stable: peers
don't need to re-discover the node after a power cycle.

## Credits

The Reticulum protocol implementation in RTReticulum is based on
[microReticulum](https://github.com/attermann/microReticulum) by Chad
Attermann, a C++ port of the [Reticulum Network
Stack](https://github.com/markqvist/Reticulum) by Mark Qvist.
RTReticulum builds on microReticulum's protocol core (Identity,
Destination, Packet, Link, Transport) and adapts it for FreeRTOS with
hardware-accelerated crypto, RadioLib radio drivers, and power-managed
embedded operation.
