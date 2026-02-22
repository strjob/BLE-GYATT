/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Общие определения и включения для SOVA BLE Sensor прошивки.
 */
#ifndef COMMON_H
#define COMMON_H

/* Стандартные библиотеки */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ESP-IDF */
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

/* FreeRTOS */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* NimBLE */
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

/* Логирование */
#define TAG "SOVA_BLE"

/* Имя устройства — перезаписывается в runtime как "SOVA-XXXX" */
#define DEVICE_NAME_DEFAULT "SOVA-????"
#define DEVICE_NAME_MAX_LEN 16

/*
 * SOVA GATT UUID (128-bit, кастомные).
 * Совпадают с config.json в sova-tauri.
 *
 * Service:   33904903-971A-442F-803B-ABB332FCF9D2
 * TX (W):    ECFC5128-3AE4-4A07-A46D-57423FD44703  (App -> Sensor, Write)
 * RX (N):    04B66E35-71D6-4E89-B43D-E83E2AB2CD29  (Sensor -> App, Notify)
 *
 * NimBLE хранит UUID в little-endian порядке байт.
 */
#define SOVA_SERVICE_UUID \
    BLE_UUID128_INIT(0xd2, 0xf9, 0xfc, 0x32, 0xb3, 0xab, 0x3b, 0x80, \
                     0x2f, 0x44, 0x1a, 0x97, 0x03, 0x49, 0x90, 0x33)

#define SOVA_TX_CHR_UUID \
    BLE_UUID128_INIT(0x03, 0x47, 0xd4, 0x3f, 0x42, 0x57, 0x6d, 0xa4, \
                     0x07, 0x4a, 0xe4, 0x3a, 0x28, 0x51, 0xfc, 0xec)

#define SOVA_RX_CHR_UUID \
    BLE_UUID128_INIT(0x29, 0xcd, 0xb2, 0x2a, 0x3e, 0xe8, 0x3d, 0xb4, \
                     0x89, 0x4e, 0xd6, 0x71, 0x35, 0x6e, 0xb6, 0x04)

#endif /* COMMON_H */
