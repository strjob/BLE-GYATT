/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Реализация датчика DHT22 (AM2302) через GPIO bit-bang (UncleRus/esp-idf-lib).
 *
 * DHT22 нельзя опрашивать чаще раз в 2 секунды — при более частых
 * запросах возвращается кешированное значение предыдущего успешного чтения.
 */
#include "sensor.h"
#include "common.h"
#include "dht.h"
#include "esp_timer.h"

#define DHT_GPIO            ((gpio_num_t)CONFIG_SENSOR_DHT22_GPIO)
#define DHT_MIN_INTERVAL_US (2000 * 1000) /* 2 секунды */

static sensor_reading_t s_last_reading = {0};
static int64_t          s_last_read_us = -DHT_MIN_INTERVAL_US; /* разрешить первое чтение */

esp_err_t sensor_init(void) {
    gpio_set_direction(DHT_GPIO, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(DHT_GPIO, 1);
    ESP_LOGI(TAG, "DHT22 инициализирован (GPIO %d, bit-bang)", CONFIG_SENSOR_DHT22_GPIO);
    return ESP_OK;
}

esp_err_t sensor_read(sensor_reading_t *reading) {
    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now = esp_timer_get_time();
    if (now - s_last_read_us < DHT_MIN_INTERVAL_US) {
        *reading = s_last_reading;
        return ESP_OK;
    }

    esp_err_t ret = dht_read_float_data(DHT_TYPE_AM2301, DHT_GPIO,
                                        &reading->humidity, &reading->temperature);
    if (ret == ESP_OK) {
        s_last_reading = *reading;
        s_last_read_us = now;
    } else {
        ESP_LOGW(TAG, "DHT22 read failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

const char *sensor_get_type(void) {
#if defined(CONFIG_SENSOR_OUTPUT_TEMP_ONLY)
    return "DHT22_T";
#elif defined(CONFIG_SENSOR_OUTPUT_HUM_ONLY)
    return "DHT22_H";
#else
    return "DHT22";
#endif
}
