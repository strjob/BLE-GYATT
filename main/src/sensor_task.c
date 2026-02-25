/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Периодическая задача отправки данных датчика.
 *
 * При активной подписке: sensor_read() → формат AD сообщения → gatt_svc_notify().
 * При неактивной подписке: idle (vTaskDelay 1 сек, экономия CPU).
 *
 * Потокобезопасность:
 *   - s_subscribed / s_interval_ms: volatile single-word, атомарны на 32-bit
 *   - s_subscriber_to / s_subscriber_from: защищены portMUX spinlock
 */
#include "sensor_task.h"
#include "common.h"
#include "gap.h"
#include "sensor.h"
#include "gatt_svc.h"
#include "subas_handler.h"

#define SUBSCRIBER_MAX_LEN 64
#define MIN_INTERVAL_MS    100
#define IDLE_DELAY_MS      5000

/* Состояние подписки */
static volatile bool s_subscribed = false;
static volatile uint32_t s_interval_ms = CONFIG_SENSOR_DEFAULT_INTERVAL_MS;

/* Адрес подписчика (куда отправлять AD сообщения) */
static char s_subscriber[SUBSCRIBER_MAX_LEN] = {0};
static portMUX_TYPE s_subscriber_mux = portMUX_INITIALIZER_UNLOCKED;

/*
 * Основной цикл задачи.
 * Если подписка активна — читает датчик и отправляет AD.
 * Если нет — спит 1 секунду.
 */
static void sensor_task_fn(void *param) {
    ESP_LOGI(TAG, "sensor task started (interval=%ldms)",
             (long)s_interval_ms);

    uint8_t msg_buf[SUBAS_MAX_MSG_LEN];

    while (1) {
        if (!s_subscribed) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_DELAY_MS));
            continue;
        }

        /* Чтение датчика */
        sensor_reading_t reading;
        esp_err_t err = sensor_read(&reading);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "sensor_read error: %d", err);
            vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
            continue;
        }

        /* Сформировать AD сообщение: #subscriber/SENSOR/AD/temp/hum/rssi$ */
        char subscriber[SUBSCRIBER_MAX_LEN];

        portENTER_CRITICAL(&s_subscriber_mux);
        strncpy(subscriber, s_subscriber, sizeof(subscriber) - 1);
        subscriber[sizeof(subscriber) - 1] = '\0';
        portEXIT_CRITICAL(&s_subscriber_mux);

        /* Измеряем RSSI подключённого клиента */
        int8_t rssi = 0;
        uint16_t ch = gatt_svc_get_conn_handle();
        if (ch != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_conn_rssi(ch, &rssi);
        }

        int written = snprintf((char *)msg_buf, sizeof(msg_buf),
                               "#%s/%s/AD/%.1f/%.1f/%d$",
                               subscriber, gap_get_own_mac(),
                               reading.temperature, reading.humidity,
                               (int)rssi);

        if (written > 0 && written < (int)sizeof(msg_buf)) {
            ESP_LOGI(TAG, "AD: %.*s", written, (char *)msg_buf);
            gatt_svc_notify(msg_buf, (uint16_t)written);
        }

        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
    }

    vTaskDelete(NULL);
}

esp_err_t sensor_task_init(void) {
    BaseType_t ret = xTaskCreate(sensor_task_fn, "Sensor Task",
                                 3 * 1024, NULL, 4, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "не удалось создать sensor task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "sensor task создана");
    return ESP_OK;
}

void sensor_task_set_subscriber(const char *subscriber) {
    portENTER_CRITICAL(&s_subscriber_mux);
    strncpy(s_subscriber, subscriber, SUBSCRIBER_MAX_LEN - 1);
    s_subscriber[SUBSCRIBER_MAX_LEN - 1] = '\0';
    portEXIT_CRITICAL(&s_subscriber_mux);
}

void sensor_task_subscribe(bool enable) {
    s_subscribed = enable;
    ESP_LOGI(TAG, "подписка %s", enable ? "включена" : "выключена");
}

uint32_t sensor_task_set_interval(uint32_t interval_ms) {
    if (interval_ms < MIN_INTERVAL_MS) {
        interval_ms = MIN_INTERVAL_MS;
    }
    s_interval_ms = interval_ms;
    ESP_LOGI(TAG, "интервал: %ldms", (long)s_interval_ms);
    return s_interval_ms;
}

bool sensor_task_is_subscribed(void) {
    return s_subscribed;
}

uint32_t sensor_task_get_interval(void) {
    return s_interval_ms;
}
