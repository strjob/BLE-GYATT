#pragma once

#include <driver/gpio.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DHT_TYPE_DHT11 = 0,
    DHT_TYPE_AM2301,   /* DHT21, DHT22, AM2302, AM2321 */
    DHT_TYPE_SI7021
} dht_sensor_type_t;

esp_err_t dht_read_data(dht_sensor_type_t sensor_type, gpio_num_t pin,
        int16_t *humidity, int16_t *temperature);

esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type, gpio_num_t pin,
        float *humidity, float *temperature);

#ifdef __cplusplus
}
#endif
