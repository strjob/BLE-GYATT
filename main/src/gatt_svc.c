/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * SOVA GATT Service — Multi-Central.
 *
 * Один сервис с двумя характеристиками:
 *   TX (Write | Write Without Response) — приложение отправляет команды датчику
 *   RX (Notify)                         — датчик отправляет ответы приложению
 *
 * Поддержка до MAX_CLIENTS одновременных подключений.
 * Каждый клиент имеет независимое состояние notify-подписки.
 * Ответ на команду отправляется конкретному клиенту (gatt_svc_notify_to),
 * AD нотификации отправляются каждому подписчику из sensor_task.
 *
 * Потокобезопасность:
 *   s_clients[] модифицируется только из NimBLE host task (subscribe_cb, add/remove).
 *   gatt_svc_notify_to() вызывается из NimBLE task и Sensor Task — читает
 *   s_clients[] без блокировки (bool/uint16_t атомарны на 32-bit,
 *   worst case — benign failed notification при race с disconnect).
 */
#include "gatt_svc.h"
#include "common.h"
#include "subas_handler.h"

#define MAX_CLIENTS CONFIG_BT_NIMBLE_MAX_CONNECTIONS

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

/* Состояние подключённых клиентов */
typedef struct {
    uint16_t conn_handle;
    bool     connected;      /* слот занят */
    bool     notify_enabled; /* клиент подписан на RX notify */
} client_slot_t;

static client_slot_t s_clients[MAX_CLIENTS];

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
 * Получает Subas сообщение от приложения, обрабатывает и отправляет ответ
 * обратно тому же клиенту через conn_handle.
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

    /* Обработать Subas сообщение (conn_handle для routing и RSSI) */
    uint8_t output_buf[SUBAS_MAX_MSG_LEN];
    uint16_t response_len = subas_handle_message(input_buf, copied,
                                                  output_buf,
                                                  sizeof(output_buf),
                                                  conn_handle);

    /* Отправить ответ обратно тому же клиенту */
    if (response_len > 0) {
        gatt_svc_notify_to(conn_handle, output_buf, response_len);
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

/* Отправить нотификацию конкретному клиенту */
void gatt_svc_notify_to(uint16_t conn_handle, const uint8_t *data,
                         uint16_t len) {
    /* Найти слот клиента и проверить подписку */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i].connected &&
            s_clients[i].conn_handle == conn_handle) {
            if (!s_clients[i].notify_enabled) {
                ESP_LOGW(TAG, "notify_to: клиент conn=%d не подписан",
                         conn_handle);
                return;
            }

            struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
            if (om == NULL) {
                ESP_LOGE(TAG, "notify_to: не удалось выделить mbuf");
                return;
            }

            int rc = ble_gatts_notify_custom(conn_handle,
                                              rx_chr_val_handle, om);
            if (rc != 0) {
                ESP_LOGE(TAG, "notify_to: ошибка conn=%d: %d",
                         conn_handle, rc);
            }
            return;
        }
    }

    ESP_LOGW(TAG, "notify_to: клиент conn=%d не найден", conn_handle);
}

/* Отправить нотификацию всем подписанным клиентам */
void gatt_svc_notify_all(const uint8_t *data, uint16_t len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i].connected && s_clients[i].notify_enabled) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
            if (om == NULL) {
                ESP_LOGE(TAG, "notify_all: не удалось выделить mbuf");
                return;
            }

            int rc = ble_gatts_notify_custom(s_clients[i].conn_handle,
                                              rx_chr_val_handle, om);
            if (rc != 0) {
                ESP_LOGE(TAG, "notify_all: ошибка conn=%d: %d",
                         s_clients[i].conn_handle, rc);
            }
        }
    }
}

/* Зарегистрировать новое подключение */
void gatt_svc_add_client(uint16_t conn_handle) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!s_clients[i].connected) {
            s_clients[i].conn_handle = conn_handle;
            s_clients[i].connected = true;
            s_clients[i].notify_enabled = false;
            ESP_LOGI(TAG, "клиент добавлен: conn=%d слот=%d",
                     conn_handle, i);
            return;
        }
    }
    ESP_LOGW(TAG, "нет свободных слотов для conn=%d", conn_handle);
}

/* Удалить клиента при отключении */
void gatt_svc_remove_client(uint16_t conn_handle) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i].connected &&
            s_clients[i].conn_handle == conn_handle) {
            s_clients[i].connected = false;
            s_clients[i].notify_enabled = false;
            ESP_LOGI(TAG, "клиент удалён: conn=%d слот=%d",
                     conn_handle, i);
            return;
        }
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
    if (event->subscribe.attr_handle != rx_chr_val_handle) {
        return;
    }

    uint16_t ch = event->subscribe.conn_handle;
    bool enabled = event->subscribe.cur_notify;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i].connected && s_clients[i].conn_handle == ch) {
            s_clients[i].notify_enabled = enabled;
            ESP_LOGI(TAG, "RX notify %s: conn=%d слот=%d",
                     enabled ? "включён" : "выключен", ch, i);
            return;
        }
    }

    ESP_LOGW(TAG, "subscribe: клиент conn=%d не найден", ch);
}

/* Инициализация GATT сервиса */
int gatt_svc_init(void) {
    int rc;

    /* Очистить таблицу клиентов */
    memset(s_clients, 0, sizeof(s_clients));

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

    ESP_LOGI(TAG, "GATT сервис инициализирован (макс. %d клиентов)",
             MAX_CLIENTS);
    return 0;
}
