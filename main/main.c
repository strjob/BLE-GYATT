/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * SOVA BLE Sensor — тестовая прошивка для ESP32-C6.
 *
 * Реализует BLE GATT сервер с SOVA Service:
 *   TX характеристика (Write)  — приём Subas команд от приложения
 *   RX характеристика (Notify) — отправка Subas ответов приложению
 *
 * Поддерживаемые Subas операции:
 *   PING     -> PONG
 *   GET_INFO -> INFO с JSON
 *   *        -> Echo (свап TO/FROM)
 *
 * LED-индикация:
 *   Мигает  — advertising (ожидание подключения)
 *   Горит   — подключён клиент
 */
#include "common.h"
#include "gap.h"
#include "gatt_svc.h"
#include "led.h"
#include "sensor.h"
#include "sensor_task.h"

/* Прототипы */
void ble_store_config_init(void);
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);
static void status_led_task(void *param);

/* Callback: хост сбросил BLE стек */
static void on_stack_reset(int reason) {
    ESP_LOGW(TAG, "NimBLE stack reset, reason: %d", reason);
}

/* Callback: хост синхронизировался с контроллером — запуск advertising */
static void on_stack_sync(void) {
    adv_init();
}

/* Конфигурация NimBLE хоста */
static void nimble_host_config_init(void) {
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Предпочитаемый MTU: 247 (макс. для BLE 5 / 2M PHY) */
    ble_att_set_preferred_mtu(247);

    ble_store_config_init();
}

/* Задача NimBLE хоста — event loop */
static void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    vTaskDelete(NULL);
}

/*
 * Задача LED-индикации состояния.
 * Мигание — advertising (не подключён).
 * Постоянно горит — подключён.
 */
static void status_led_task(void *param) {
    ESP_LOGI(TAG, "status LED task started");

    while (1) {
        if (gap_is_connected()) {
            /* Подключён — LED горит постоянно */
            led_on();
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            /* Advertising — LED мигает */
            led_on();
            vTaskDelay(pdMS_TO_TICKS(200));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }

    vTaskDelete(NULL);
}

void app_main(void) {
    int rc;
    esp_err_t ret;

    /* Инициализация LED */
    led_init();

    /* Инициализация NVS (используется BLE стеком для хранения) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %d", ret);
        return;
    }

    /* Инициализация датчика */
    ret = sensor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sensor init failed: %d", ret);
        return;
    }

    /* Инициализация NimBLE стека */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %d", ret);
        return;
    }

    /* GAP инициализация */
#if CONFIG_BT_NIMBLE_GAP_SERVICE
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "GAP init failed: %d", rc);
        return;
    }
#endif

    /* GATT сервис */
    rc = gatt_svc_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT init failed: %d", rc);
        return;
    }

    /* Конфигурация NimBLE хоста */
    nimble_host_config_init();

    /* Запуск задач */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);
    xTaskCreate(status_led_task, "Status LED", 2 * 1024, NULL, 3, NULL);

    /* Задача периодической отправки данных датчика */
    sensor_task_init();

    ESP_LOGI(TAG, "SOVA BLE Sensor initialized");
}
