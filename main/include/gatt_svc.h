/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * SOVA GATT Service — TX/RX характеристики для Subas протокола.
 *
 * Multi-Central: поддержка до CONFIG_BT_NIMBLE_MAX_CONNECTIONS клиентов.
 * Каждый клиент имеет свой conn_handle и независимое состояние notify.
 */
#ifndef GATT_SVR_H
#define GATT_SVR_H

/* NimBLE GATT APIs */
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

/* NimBLE GAP APIs */
#include "host/ble_gap.h"

/* Инициализация GATT сервиса (вызывать после nimble_port_init) */
int gatt_svc_init(void);

/* Отправить нотификацию конкретному клиенту по conn_handle.
 * Проверяет, что клиент подписан на RX notify. */
void gatt_svc_notify_to(uint16_t conn_handle, const uint8_t *data,
                         uint16_t len);

/* Отправить нотификацию всем подписанным клиентам */
void gatt_svc_notify_all(const uint8_t *data, uint16_t len);

/* Зарегистрировать новое подключение (вызывается из gap.c при BLE_GAP_EVENT_CONNECT) */
void gatt_svc_add_client(uint16_t conn_handle);

/* Удалить клиента при отключении (вызывается из gap.c при BLE_GAP_EVENT_DISCONNECT) */
void gatt_svc_remove_client(uint16_t conn_handle);

/* Callback регистрации GATT сервисов (для логирования) */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

/* Callback подписки клиента на нотификации */
void gatt_svr_subscribe_cb(struct ble_gap_event *event);

#endif /* GATT_SVR_H */
