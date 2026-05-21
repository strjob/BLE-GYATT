/*
 * Copyright (c) 2016 Jonathan Hartsuiker <https://github.com/jsuiker>
 * Copyright (c) 2018 Ruslan V. Uss <unclerus@gmail.com>
 *
 * BSD Licensed
 *
 * ESP-IDF driver for DHT11, AM2301 (DHT21, DHT22, AM2302, AM2321)
 * Ported from esp-open-rtos, adapted for ESP32-C6 / RISC-V targets.
 */
#include "dht.h"

#include <freertos/FreeRTOS.h>
#include <string.h>
#include <esp_log.h>
#include <rom/ets_sys.h>

#define DHT_TIMER_INTERVAL 2
#define DHT_DATA_BITS  40
#define DHT_DATA_BYTES (DHT_DATA_BITS / 8)

static const char *TAG = "dht";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#define PORT_ENTER_CRITICAL() portENTER_CRITICAL(&s_mux)
#define PORT_EXIT_CRITICAL()  portEXIT_CRITICAL(&s_mux)

#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

#define CHECK_LOGE(x, msg, ...) do { \
        esp_err_t __; \
        if ((__ = x) != ESP_OK) { \
            PORT_EXIT_CRITICAL(); \
            ESP_LOGE(TAG, msg, ## __VA_ARGS__); \
            return __; \
        } \
    } while (0)

static esp_err_t dht_await_pin_state(gpio_num_t pin, uint32_t timeout,
        int expected_pin_state, uint32_t *duration)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    for (uint32_t i = 0; i < timeout; i += DHT_TIMER_INTERVAL) {
        ets_delay_us(DHT_TIMER_INTERVAL);
        if (gpio_get_level(pin) == expected_pin_state) {
            if (duration)
                *duration = i;
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static inline esp_err_t dht_fetch_data(dht_sensor_type_t sensor_type,
        gpio_num_t pin, uint8_t data[DHT_DATA_BYTES])
{
    uint32_t low_duration;
    uint32_t high_duration;

    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 0);
    ets_delay_us(sensor_type == DHT_TYPE_SI7021 ? 500 : 20000);
    gpio_set_level(pin, 1);

    CHECK_LOGE(dht_await_pin_state(pin, 40, 0, NULL),
            "Init error phase B");
    CHECK_LOGE(dht_await_pin_state(pin, 88, 1, NULL),
            "Init error phase C");
    CHECK_LOGE(dht_await_pin_state(pin, 88, 0, NULL),
            "Init error phase D");

    for (int i = 0; i < DHT_DATA_BITS; i++) {
        CHECK_LOGE(dht_await_pin_state(pin, 65, 1, &low_duration),
                "LOW bit timeout");
        CHECK_LOGE(dht_await_pin_state(pin, 75, 0, &high_duration),
                "HIGH bit timeout");

        uint8_t b = i / 8;
        uint8_t m = i % 8;
        if (!m)
            data[b] = 0;

        data[b] |= (high_duration > low_duration) << (7 - m);
    }

    return ESP_OK;
}

static inline int16_t dht_convert_data(dht_sensor_type_t sensor_type,
        uint8_t msb, uint8_t lsb)
{
    int16_t data;
    if (sensor_type == DHT_TYPE_DHT11) {
        data = msb * 10;
    } else {
        data = msb & 0x7F;
        data <<= 8;
        data |= lsb;
        if (msb & BIT(7))
            data = -data;
    }
    return data;
}

esp_err_t dht_read_data(dht_sensor_type_t sensor_type, gpio_num_t pin,
        int16_t *humidity, int16_t *temperature)
{
    CHECK_ARG(humidity || temperature);

    uint8_t data[DHT_DATA_BYTES] = { 0 };

    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 1);

    PORT_ENTER_CRITICAL();
    esp_err_t result = dht_fetch_data(sensor_type, pin, data);
    if (result == ESP_OK)
        PORT_EXIT_CRITICAL();

    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 1);

    if (result != ESP_OK)
        return result;

    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        ESP_LOGE(TAG, "Checksum failed");
        return ESP_ERR_INVALID_CRC;
    }

    if (humidity)
        *humidity = dht_convert_data(sensor_type, data[0], data[1]);
    if (temperature)
        *temperature = dht_convert_data(sensor_type, data[2], data[3]);

    return ESP_OK;
}

esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type, gpio_num_t pin,
        float *humidity, float *temperature)
{
    CHECK_ARG(humidity || temperature);

    int16_t i_humidity, i_temp;
    esp_err_t res = dht_read_data(sensor_type, pin,
            humidity ? &i_humidity : NULL,
            temperature ? &i_temp : NULL);
    if (res != ESP_OK)
        return res;

    if (humidity)
        *humidity = i_humidity / 10.0f;
    if (temperature)
        *temperature = i_temp / 10.0f;

    return ESP_OK;
}
