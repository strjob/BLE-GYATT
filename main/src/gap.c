/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * GAP — advertising с SOVA Service UUID, управление соединениями.
 *
 * Advertisement data:  Flags + 128-bit Service UUID
 * Scan response data:  Complete Local Name ("SOVA-XXXX")
 */
#include "gap.h"
#include "common.h"
#include "gatt_svc.h"
#include "sensor_task.h"

/* Прототипы приватных функций */
inline static void format_addr(char *addr_str, uint8_t addr[]);
static void print_conn_desc(struct ble_gap_conn_desc *desc);
static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

/* Приватные переменные */
static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};
static char device_name[DEVICE_NAME_MAX_LEN] = DEVICE_NAME_DEFAULT;
static volatile bool peer_connected = false;

/* UUID сервиса для включения в advertisement */
static ble_uuid128_t adv_svc_uuid = SOVA_SERVICE_UUID;

/* Форматирование BLE адреса для логов */
inline static void format_addr(char *addr_str, uint8_t addr[]) {
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[5], addr[4],
            addr[3], addr[2], addr[1], addr[0]);
}

/* Вывод информации о соединении */
static void print_conn_desc(struct ble_gap_conn_desc *desc) {
    char addr_str[18] = {0};

    ESP_LOGI(TAG, "conn handle: %d", desc->conn_handle);

    format_addr(addr_str, desc->our_id_addr.val);
    ESP_LOGI(TAG, "our addr: type=%d, %s", desc->our_id_addr.type, addr_str);

    format_addr(addr_str, desc->peer_id_addr.val);
    ESP_LOGI(TAG, "peer addr: type=%d, %s", desc->peer_id_addr.type, addr_str);

    ESP_LOGI(TAG,
             "itvl=%d latency=%d timeout=%d "
             "enc=%d auth=%d bonded=%d",
             desc->conn_itvl, desc->conn_latency, desc->supervision_timeout,
             desc->sec_state.encrypted, desc->sec_state.authenticated,
             desc->sec_state.bonded);
}

/*
 * Настройка и запуск advertising.
 *
 * Advertisement packet:
 *   - Flags: General Discoverable + BR/EDR Not Supported
 *   - 128-bit Service UUID (SOVA_SERVICE_UUID)
 *
 * Scan response packet:
 *   - Complete Local Name ("SOVA-XXXX")
 *
 * Интервал: 100ms (быстро для тестирования)
 */
static void start_advertising(void) {
    int rc;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params adv_params = {0};

    /* === Advertisement data === */

    /* Flags: General Discoverable + BR/EDR Not Supported */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 128-bit SOVA Service UUID — для фильтрации при сканировании */
    adv_fields.uuids128 = &adv_svc_uuid;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv set fields failed: %d", rc);
        return;
    }

    /* === Scan response data === */

    /* Имя устройства */
    rsp_fields.name = (uint8_t *)device_name;
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;

    /* TX Power */
    rsp_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    rsp_fields.tx_pwr_lvl_is_present = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan rsp set fields failed: %d", rc);
        return;
    }

    /* === Параметры advertising === */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(150);

    /* Запуск */
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising started: %s", device_name);
}

/*
 * Обработчик GAP событий.
 */
static int gap_event_handler(struct ble_gap_event *event, void *arg) {
    int rc = 0;
    struct ble_gap_conn_desc desc;

    switch (event->type) {

    /* Новое подключение или ошибка подключения */
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) {
            peer_connected = true;

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc != 0) {
                ESP_LOGE(TAG, "conn_find failed: %d", rc);
                return rc;
            }
            print_conn_desc(&desc);

            /*
             * Запросить параметры соединения для активного режима:
             *   interval 20-50ms, latency 0, timeout 4000ms
             */
            struct ble_gap_upd_params params = {
                .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN, /* ~30ms */
                .itvl_max = 40,                             /* 50ms */
                .latency = 4,              /* Пропуск до 4 connection events — экономия radio ~75% */
                .supervision_timeout = 400, /* 4000ms > (1+4)*50ms*2 = 500ms */
            };
            rc = ble_gap_update_params(event->connect.conn_handle, &params);
            if (rc != 0) {
                ESP_LOGW(TAG, "conn params update failed: %d (не критично)",
                         rc);
            }
        } else {
            peer_connected = false;
            start_advertising();
        }
        return rc;

    /* Отключение */
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected; reason=%d", event->disconnect.reason);
        peer_connected = false;
        sensor_task_subscribe(false);
        start_advertising();
        return rc;

    /* Обновление параметров соединения */
    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "conn updated; status=%d", event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc == 0) {
            print_conn_desc(&desc);
        }
        return rc;

    /* Advertising завершён — перезапуск */
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "adv complete; reason=%d", event->adv_complete.reason);
        start_advertising();
        return rc;

    /* Нотификация отправлена */
    case BLE_GAP_EVENT_NOTIFY_TX:
        if (event->notify_tx.status != 0 &&
            event->notify_tx.status != BLE_HS_EDONE) {
            ESP_LOGW(TAG, "notify tx error: conn=%d attr=%d status=%d",
                     event->notify_tx.conn_handle,
                     event->notify_tx.attr_handle,
                     event->notify_tx.status);
        }
        return rc;

    /* Подписка на нотификации */
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe: conn=%d attr=%d reason=%d "
                 "notify=%d->%d indicate=%d->%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle,
                 event->subscribe.reason,
                 event->subscribe.prev_notify, event->subscribe.cur_notify,
                 event->subscribe.prev_indicate,
                 event->subscribe.cur_indicate);
        gatt_svr_subscribe_cb(event);
        return rc;

    /* MTU обновлён */
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: conn=%d cid=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.channel_id,
                 event->mtu.value);
        return rc;
    }

    return rc;
}

/*
 * Инициализация advertising.
 * Вызывается из on_stack_sync — стек BLE уже синхронизирован.
 * Здесь получаем MAC-адрес и формируем имя "SOVA-XXXX".
 */
void adv_init(void) {
    int rc;
    char addr_str[18] = {0};

    /* Убедиться, что есть BT адрес */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "no BT address available!");
        return;
    }

    /* Определить тип адреса */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "addr type infer failed: %d", rc);
        return;
    }

    /* Скопировать адрес */
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "addr copy failed: %d", rc);
        return;
    }

    format_addr(addr_str, addr_val);
    ESP_LOGI(TAG, "BLE address: %s", addr_str);

    /* Сформировать имя "SOVA-XXXX" из последних 2 байт MAC */
    snprintf(device_name, sizeof(device_name), "SOVA-%02X%02X",
             addr_val[1], addr_val[0]);
    ble_svc_gap_device_name_set(device_name);
    ESP_LOGI(TAG, "device name: %s", device_name);

    /* Запустить advertising */
    start_advertising();
}

/* Инициализация GAP сервиса */
int gap_init(void) {
    int rc;

    ble_svc_gap_init();

    /* Временное имя — будет перезаписано в adv_init() */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME_DEFAULT);
    if (rc != 0) {
        ESP_LOGE(TAG, "set device name failed: %d", rc);
        return rc;
    }

    return 0;
}

/* Проверить, подключён ли клиент */
bool gap_is_connected(void) {
    return peer_connected;
}
