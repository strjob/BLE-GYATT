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
#include "esp_pm.h"
#include "gap.h"
#include "gatt_svc.h"
#include "led.h"
#include "sensor.h"
#include "sensor_task.h"

/* Прототипы */
void ble_store_config_init(void);
static esp_err_t power_management_init(void);
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);
static void status_led_task(void *param);

/* Инициализация power management: light sleep между BLE событиями */
static esp_err_t power_management_init(void) {
#if CONFIG_SOVA_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_SOVA_PM_MAX_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_SOVA_PM_MIN_CPU_FREQ_MHZ,
        .light_sleep_enable = true,
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PM configure failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Power management: light sleep (max=%dMHz, min=%dMHz)",
             CONFIG_SOVA_PM_MAX_CPU_FREQ_MHZ, CONFIG_SOVA_PM_MIN_CPU_FREQ_MHZ);
#else
    ESP_LOGI(TAG, "Power management: disabled");
#endif
    return ESP_OK;
}

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

    bool was_connected = false;

    while (1) {
        if (gap_is_connected()) {
            /* Подключён — LED горит постоянно, спим подольше для экономии */
            if (!was_connected) {
                led_on();
                was_connected = true;
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
        } else {
            was_connected = false;
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

    /* Power management — до NimBLE, чтобы BLE контроллер интегрировался с PM */
    ret = power_management_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PM init failed, продолжаем без энергосбережения");
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
