/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Периодическая задача отправки данных датчика по BLE.
 *
 * Управляет подпиской: при активной подписке читает датчик
 * и отправляет AD (auto-data) сообщения через gatt_svc_notify().
 */
#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Создать FreeRTOS задачу периодической отправки */
esp_err_t sensor_task_init(void);

/* Установить адрес подписчика (FROM из W/ON команды) */
void sensor_task_set_subscriber(const char *subscriber);

/* Включить/выключить подписку */
void sensor_task_subscribe(bool enable);

/* Установить интервал отправки (мс). Возвращает фактический интервал. */
uint32_t sensor_task_set_interval(uint32_t interval_ms);

/* Текущее состояние подписки */
bool sensor_task_is_subscribed(void);

/* Текущий интервал отправки (мс) */
uint32_t sensor_task_get_interval(void);

#endif /* SENSOR_TASK_H */
