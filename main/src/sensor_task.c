/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Периодическая задача отправки данных датчика — Multi-Central.
 *
 * Каждый Central может независимо подписаться (W/ON) и получать
 * AD нотификации через свой BLE connection handle.
 * При отсутствии подписчиков задача спит (экономия CPU).
 *
 * Потокобезопасность:
 *   - s_interval_ms: volatile single-word, атомарен на 32-bit
 *   - s_subs[]: защищён portMUX spinlock (доступ из NimBLE task и Sensor Task)
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
#define MAX_SUBS           CONFIG_BT_NIMBLE_MAX_CONNECTIONS

/* Подписка одного клиента */
typedef struct {
    char     from[SUBSCRIBER_MAX_LEN]; /* FROM адрес Central */
    uint16_t conn_handle;               /* через какое BLE соединение слать */
    bool     active;                    /* подписка активна */
} subscription_t;

/* Таблица подписок */
static subscription_t s_subs[MAX_SUBS];
static portMUX_TYPE s_subs_mux = portMUX_INITIALIZER_UNLOCKED;

/* Глобальный интервал (общий для всех подписчиков) */
static volatile uint32_t s_interval_ms = CONFIG_SENSOR_DEFAULT_INTERVAL_MS;

/* Проверить, есть ли хотя бы один активный подписчик (без блокировки) */
static bool any_subscriber_active(void) {
    for (int i = 0; i < MAX_SUBS; i++) {
        if (s_subs[i].active) {
            return true;
        }
    }
    return false;
}

/*
 * Основной цикл задачи.
 * Если есть подписчики — читает датчик и отправляет AD каждому.
 * Если нет — спит.
 */
static void sensor_task_fn(void *param) {
    ESP_LOGI(TAG, "sensor task started (interval=%ldms)",
             (long)s_interval_ms);

    uint8_t msg_buf[SUBAS_MAX_MSG_LEN];

    while (1) {
        if (!any_subscriber_active()) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_DELAY_MS));
            continue;
        }

        /* Одно чтение датчика для всех подписчиков */
        sensor_reading_t reading;
        esp_err_t err = sensor_read(&reading);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "sensor_read error: %d", err);
            vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
            continue;
        }

        /* Отправить AD каждому активному подписчику */
        for (int i = 0; i < MAX_SUBS; i++) {
            char from[SUBSCRIBER_MAX_LEN];
            uint16_t ch;

            portENTER_CRITICAL(&s_subs_mux);
            if (!s_subs[i].active) {
                portEXIT_CRITICAL(&s_subs_mux);
                continue;
            }
            strncpy(from, s_subs[i].from, sizeof(from) - 1);
            from[sizeof(from) - 1] = '\0';
            ch = s_subs[i].conn_handle;
            portEXIT_CRITICAL(&s_subs_mux);

            /* RSSI для конкретного клиента */
            int8_t rssi = 0;
            ble_gap_conn_rssi(ch, &rssi);

            int written = snprintf(
                (char *)msg_buf, sizeof(msg_buf),
                "#%s/%s/AD/%.1f/%.1f/%d$",
                from, gap_get_own_mac(),
                reading.temperature, reading.humidity, (int)rssi);

            if (written > 0 && written < (int)sizeof(msg_buf)) {
                ESP_LOGI(TAG, "AD[%d]: %.*s", i, written, (char *)msg_buf);
                gatt_svc_notify_to(ch, msg_buf, (uint16_t)written);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
    }

    vTaskDelete(NULL);
}

esp_err_t sensor_task_init(void) {
    memset(s_subs, 0, sizeof(s_subs));

    BaseType_t ret = xTaskCreate(sensor_task_fn, "Sensor Task",
                                 3 * 1024, NULL, 4, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "не удалось создать sensor task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "sensor task создана (макс. %d подписчиков)", MAX_SUBS);
    return ESP_OK;
}

void sensor_task_add_subscriber(const char *from, uint16_t conn_handle) {
    portENTER_CRITICAL(&s_subs_mux);

    /* Обновить существующий слот для этого conn_handle */
    for (int i = 0; i < MAX_SUBS; i++) {
        if (s_subs[i].active && s_subs[i].conn_handle == conn_handle) {
            strncpy(s_subs[i].from, from, SUBSCRIBER_MAX_LEN - 1);
            s_subs[i].from[SUBSCRIBER_MAX_LEN - 1] = '\0';
            portEXIT_CRITICAL(&s_subs_mux);
            ESP_LOGI(TAG, "подписка обновлена: conn=%d from=%s",
                     conn_handle, from);
            return;
        }
    }

    /* Найти свободный слот */
    for (int i = 0; i < MAX_SUBS; i++) {
        if (!s_subs[i].active) {
            strncpy(s_subs[i].from, from, SUBSCRIBER_MAX_LEN - 1);
            s_subs[i].from[SUBSCRIBER_MAX_LEN - 1] = '\0';
            s_subs[i].conn_handle = conn_handle;
            s_subs[i].active = true;
            portEXIT_CRITICAL(&s_subs_mux);
            ESP_LOGI(TAG, "подписка добавлена: conn=%d from=%s слот=%d",
                     conn_handle, from, i);
            return;
        }
    }

    portEXIT_CRITICAL(&s_subs_mux);
    ESP_LOGW(TAG, "нет свободных слотов подписки для conn=%d", conn_handle);
}

void sensor_task_remove_subscriber(uint16_t conn_handle) {
    portENTER_CRITICAL(&s_subs_mux);
    for (int i = 0; i < MAX_SUBS; i++) {
        if (s_subs[i].active && s_subs[i].conn_handle == conn_handle) {
            s_subs[i].active = false;
            portEXIT_CRITICAL(&s_subs_mux);
            ESP_LOGI(TAG, "подписка удалена: conn=%d слот=%d",
                     conn_handle, i);
            return;
        }
    }
    portEXIT_CRITICAL(&s_subs_mux);
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
    return any_subscriber_active();
}

uint32_t sensor_task_get_interval(void) {
    return s_interval_ms;
}
