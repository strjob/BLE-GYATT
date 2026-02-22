# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SOVA BLE Sensor — тестовая прошивка для ESP32-C6. Реализует BLE GATT сервер с кастомным SOVA Service для тестирования BLE-транспорта в sova-tauri.

Протокол: Subas (`#TO/FROM/OP/DATA$` или `#TO/FROM/OP$`) поверх BLE характеристик TX (Write) и RX (Notify).

Targets: ESP32, ESP32-C2, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-C61, ESP32-H2, ESP32-S3.

## Build Commands

```bash
idf.py set-target esp32c6          # целевой чип
idf.py build                       # сборка
idf.py -p <PORT> flash monitor     # прошивка + серийный монитор (Ctrl-] для выхода)
idf.py menuconfig                  # конфигурация (LED, GPIO)
```

Requires ESP-IDF toolchain. A devcontainer (`espressif/idf` Docker image) is provided in `.devcontainer/`.

## Architecture

Entry point is `app_main()` in `main/main.c`. Initialization order:
1. LED init -> NVS flash init -> NimBLE port init -> GAP init -> GATT init -> NimBLE host config
2. Two FreeRTOS tasks: `nimble_host_task` (NimBLE event loop) and `status_led_task` (LED state indication)

### Module Responsibilities

- **`main/main.c`** — Entry point, task creation, NimBLE host configuration
- **`main/src/gap.c`** — GAP: advertising с SOVA Service UUID, connection management, device name "SOVA-XXXX"
- **`main/src/gatt_svc.c`** — GATT: SOVA Service с TX/RX характеристиками, notify подписки
- **`main/src/subas_handler.c`** — Subas protocol parser: PING/PONG, GET_INFO, Echo
- **`main/src/led.c`** — LED abstraction (GPIO or LED strip), используется для индикации состояния
- **`main/include/common.h`** — Shared includes, SOVA UUID defines

### GATT Service

| Service | UUID (128-bit) | Characteristic | UUID (128-bit) | Permissions |
|---------|----------------|----------------|----------------|-------------|
| SOVA Sensor | 33904903-971A-442F-803B-ABB332FCF9D2 | TX (App->Sensor) | ECFC5128-3AE4-4A07-A46D-57423FD44703 | Write, Write No Rsp |
| | | RX (Sensor->App) | 04B66E35-71D6-4E89-B43D-E83E2AB2CD29 | Notify |

### Data Flow

1. App writes Subas command to TX characteristic: `#SENSOR/APP/PING$`
2. `tx_chr_access()` callback fires -> `subas_handle_message()` parses and generates response
3. Response sent via `gatt_svc_notify()` on RX characteristic: `#APP/SENSOR/PONG$`

### Subas Operations

| Command | Response | Description |
|---------|----------|-------------|
| `#SENSOR/APP/PING$` | `#APP/SENSOR/PONG$` | Connectivity test |
| `#SENSOR/APP/GET_INFO$` | `#APP/SENSOR/INFO/{...}$` | Device info (fw, type, battery) |
| `#SENSOR/APP/R$` | `#APP/SENSOR/AR/mock_ble,25.3$` | Read (тестовые данные) |
| `#SENSOR/APP/W/DATA$` | `#APP/SENSOR/AW/DATA$` | Write (подтверждение) |
| `#SENSOR/APP/*/DATA$` | `#APP/SENSOR/A/DATA$` | Echo (swap TO/FROM) |

### LED Indication

- **Blinking** (200ms on, 800ms off) — Advertising, waiting for connection
- **Solid on** — Connected to client

## Configuration

- `sdkconfig.defaults` — BLE enabled, NimBLE, MTU 247, GPIO LED pin 8
- `main/Kconfig.projbuild` — LED type (GPIO vs LED strip) and GPIO pin
- `main/idf_component.yml` — External dependency: `espressif/led_strip ^2.4.1`

## Testing with sova-tauri

UUID в прошивке совпадают с `config/config.json` в sova-tauri:
```json
{
  "service_uuid": "33904903-971a-442f-803b-abb332fcf9d2",
  "tx_char_uuid": "ecfc5128-3ae4-4a07-a46d-57423fd44703",
  "rx_char_uuid": "04b66e35-71d6-4e89-b43d-e83e2ab2cd29"
}
```

## Code Style

- C99, ESP-IDF conventions (ESP_LOGI/LOGE for logging)
- All modules include `common.h` for shared dependencies
- Headers in `main/include/`, sources in `main/src/`, entry point at `main/main.c`
- Comments in Russian
- License: Unlicense OR CC0-1.0
