/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Mock-реализация датчика температуры и влажности.
 *
 * Генерирует реалистичные данные с градуальным дрифтом:
 *   - Температура: начало 22.0°C, дрифт ±0.3°C, границы [15, 30]
 *   - Влажность: начало 55.0%, дрифт ±0.2%, границы [30, 80]
 *   - Мягкое притягивание к центру при приближении к границам
 */
#include "sensor.h"
#include "common.h"
#include "esp_random.h"

/* Начальные значения */
static float s_temperature = 22.0f;
static float s_humidity = 55.0f;

/* Границы */
#define T_MIN 15.0f
#define T_MAX 30.0f
#define T_CENTER 22.5f
#define T_DRIFT 0.3f

#define H_MIN 30.0f
#define H_MAX 80.0f
#define H_CENTER 55.0f
#define H_DRIFT 0.2f

/* Сила притягивания к центру (0.0 = нет, 1.0 = полное) */
#define PULL_STRENGTH 0.05f

/*
 * Случайное число в диапазоне [-1.0, 1.0] на основе esp_random().
 */
static float random_float(void) {
    uint32_t r = esp_random();
    return ((float)r / (float)UINT32_MAX) * 2.0f - 1.0f;
}

/*
 * Ограничить значение в диапазоне [min, max].
 */
static float clampf(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

esp_err_t sensor_init(void) {
    s_temperature = 22.0f;
    s_humidity = 55.0f;
    ESP_LOGI(TAG, "mock датчик инициализирован (T=%.1f, H=%.1f)",
             s_temperature, s_humidity);
    return ESP_OK;
}

esp_err_t sensor_read(sensor_reading_t *reading) {
    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Дрифт + мягкое притягивание к центру */
    float t_pull = (T_CENTER - s_temperature) * PULL_STRENGTH;
    s_temperature += random_float() * T_DRIFT + t_pull;
    s_temperature = clampf(s_temperature, T_MIN, T_MAX);

    float h_pull = (H_CENTER - s_humidity) * PULL_STRENGTH;
    s_humidity += random_float() * H_DRIFT + h_pull;
    s_humidity = clampf(s_humidity, H_MIN, H_MAX);

    reading->temperature = s_temperature;
    reading->humidity = s_humidity;

    return ESP_OK;
}

const char *sensor_get_type(void) {
    return "mock_th";
}
