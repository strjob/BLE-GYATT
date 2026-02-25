/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Периодическая задача отправки данных датчика по BLE.
 *
 * Multi-Central: каждый клиент может независимо подписаться (W/ON)
 * и получать AD нотификации через свой BLE connection handle.
 */
#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Создать FreeRTOS задачу периодической отправки */
esp_err_t sensor_task_init(void);

/* Добавить подписку: from = адрес Central (FROM из W/ON), conn_handle = BLE соединение */
void sensor_task_add_subscriber(const char *from, uint16_t conn_handle);

/* Удалить подписку по conn_handle (W/OFF или disconnect) */
void sensor_task_remove_subscriber(uint16_t conn_handle);

/* Установить интервал отправки (мс). Возвращает фактический интервал. */
uint32_t sensor_task_set_interval(uint32_t interval_ms);

/* Есть ли хотя бы один активный подписчик (для GET_INFO) */
bool sensor_task_is_subscribed(void);

/* Текущий интервал отправки (мс) */
uint32_t sensor_task_get_interval(void);

#endif /* SENSOR_TASK_H */
