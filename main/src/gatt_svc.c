/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * SOVA GATT Service.
 *
 * Один сервис с двумя характеристиками:
 *   TX (Write | Write Without Response) — приложение отправляет команды датчику
 *   RX (Notify)                         — датчик отправляет ответы приложению
 *
 * При записи в TX вызывается Subas-обработчик, ответ отправляется через RX notify.
 */
#include "gatt_svc.h"
#include "common.h"
#include "subas_handler.h"

/* Прототипы callback-функций для характеристик */
static int tx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);
static int rx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg);

/* UUID сервиса и характеристик */
static const ble_uuid128_t sova_svc_uuid = SOVA_SERVICE_UUID;
static const ble_uuid128_t sova_tx_chr_uuid = SOVA_TX_CHR_UUID;
static const ble_uuid128_t sova_rx_chr_uuid = SOVA_RX_CHR_UUID;

/* Handles характеристик (заполняются NimBLE при регистрации) */
static uint16_t tx_chr_val_handle;
static uint16_t rx_chr_val_handle;

/* Состояние подключения и подписки */
static uint16_t notify_conn_handle = 0;
static bool notify_conn_handle_valid = false;
static bool notify_enabled = false;

/* Таблица GATT сервисов */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /* SOVA Sensor Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &sova_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    /* TX: App -> Sensor (Write | Write Without Response) */
                    .uuid = &sova_tx_chr_uuid.u,
                    .access_cb = tx_chr_access,
                    .flags = BLE_GATT_CHR_F_WRITE |
                             BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &tx_chr_val_handle,
                },
                {
                    /* RX: Sensor -> App (Notify) */
                    .uuid = &sova_rx_chr_uuid.u,
                    .access_cb = rx_chr_access,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &rx_chr_val_handle,
                },
                {0}, /* Терминатор */
            },
    },
    {0}, /* Терминатор */
};

/*
 * Callback записи в TX характеристику.
 * Получает Subas сообщение от приложения, обрабатывает и отправляет ответ.
 */
static int tx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ESP_LOGE(TAG, "TX: неожиданная операция %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Собрать данные из mbuf-цепочки */
    uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
    if (om_len == 0 || om_len > SUBAS_MAX_MSG_LEN) {
        ESP_LOGW(TAG, "TX: некорректная длина данных: %d", om_len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t input_buf[SUBAS_MAX_MSG_LEN];
    uint16_t copied = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, input_buf, sizeof(input_buf),
                                 &copied);
    if (rc != 0) {
        ESP_LOGE(TAG, "TX: ошибка чтения mbuf: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGI(TAG, "TX write: %.*s (conn=%d, len=%d)",
             copied, (char *)input_buf, conn_handle, copied);

    /* Обработать Subas сообщение */
    uint8_t output_buf[SUBAS_MAX_MSG_LEN];
    uint16_t response_len = subas_handle_message(input_buf, copied,
                                                  output_buf,
                                                  sizeof(output_buf));

    /* Отправить ответ через RX notify */
    if (response_len > 0) {
        gatt_svc_notify(output_buf, response_len);
    }

    return 0;
}

/*
 * Callback для RX характеристики.
 * RX — только Notify, чтение клиентом не поддерживается.
 */
static int rx_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    ESP_LOGW(TAG, "RX: неожиданный прямой доступ, op=%d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

/* Отправить нотификацию подключённому клиенту через RX характеристику */
void gatt_svc_notify(const uint8_t *data, uint16_t len) {
    if (!notify_enabled || !notify_conn_handle_valid) {
        ESP_LOGW(TAG, "notify: клиент не подписан (enabled=%d, valid=%d)",
                 notify_enabled, notify_conn_handle_valid);
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "notify: не удалось выделить mbuf");
        return;
    }

    int rc = ble_gatts_notify_custom(notify_conn_handle, rx_chr_val_handle,
                                     om);
    if (rc != 0) {
        ESP_LOGE(TAG, "notify: ошибка отправки: %d", rc);
    }
}

/* Callback регистрации GATT атрибутов (логирование) */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(TAG, "GATT svc registered: %s handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(TAG, "GATT chr registered: %s def=%d val=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGI(TAG, "GATT dsc registered: %s handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    default:
        assert(0);
        break;
    }
}

/* Callback подписки клиента на нотификации RX характеристики */
void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    ESP_LOGI(TAG, "subscribe: conn=%d attr=%d notify=%d->%d indicate=%d->%d",
             event->subscribe.conn_handle, event->subscribe.attr_handle,
             event->subscribe.prev_notify, event->subscribe.cur_notify,
             event->subscribe.prev_indicate, event->subscribe.cur_indicate);

    /* Проверяем подписку на RX характеристику */
    if (event->subscribe.attr_handle == rx_chr_val_handle) {
        notify_conn_handle = event->subscribe.conn_handle;
        notify_conn_handle_valid = true;
        notify_enabled = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "RX notify %s",
                 notify_enabled ? "включён" : "выключен");
    }
}

/* Инициализация GATT сервиса */
int gatt_svc_init(void) {
    int rc;

    /* Инициализировать GATT service */
    ble_svc_gatt_init();

    /* Подсчитать конфигурацию */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count_cfg failed: %d", rc);
        return rc;
    }

    /* Добавить сервисы */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add_svcs failed: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "GATT сервис инициализирован");
    return 0;
}
