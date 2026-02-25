/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Реализация датчика DHT22 (AM2302) через RMT peripheral.
 * Аппаратный тайминг — надёжно работает с BLE стеком и light sleep.
 */
#include "sensor.h"
#include "common.h"
#include "am2302_rmt.h"

static am2302_handle_t s_sensor = NULL;

esp_err_t sensor_init(void) {
    am2302_config_t am2302_cfg = {
        .gpio_num = CONFIG_SENSOR_DHT22_GPIO,
    };
    am2302_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
    };

    esp_err_t ret = am2302_new_sensor_rmt(&am2302_cfg, &rmt_cfg, &s_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DHT22 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "DHT22 инициализирован (GPIO %d, RMT backend)",
             CONFIG_SENSOR_DHT22_GPIO);
    return ESP_OK;
}

esp_err_t sensor_read(sensor_reading_t *reading) {
    if (!reading || !s_sensor) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = am2302_read_temp_humi(s_sensor,
                                           &reading->temperature,
                                           &reading->humidity);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DHT22 read failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

const char *sensor_get_type(void) {
    return "DHT22";
}
