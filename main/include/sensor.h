/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Абстракция датчика — интерфейс для чтения температуры и влажности.
 * Конкретная реализация выбирается через Kconfig (CONFIG_SENSOR_TYPE_*).
 */
#ifndef SENSOR_H
#define SENSOR_H

#include "esp_err.h"

typedef struct {
    float temperature;  /* °C */
    float humidity;     /* % */
} sensor_reading_t;

/* Инициализация датчика */
esp_err_t sensor_init(void);

/* Одноразовое чтение показаний */
esp_err_t sensor_read(sensor_reading_t *reading);

/* Строковый идентификатор типа датчика (например "mock_th") */
const char *sensor_get_type(void);

#endif /* SENSOR_H */
