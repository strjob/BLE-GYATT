/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * SOVA GATT Service — TX/RX характеристики для Subas протокола.
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

/* Отправить нотификацию подключённому клиенту через RX характеристику */
void gatt_svc_notify(const uint8_t *data, uint16_t len);

/* Callback регистрации GATT сервисов (для логирования) */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

/* Callback подписки клиента на нотификации */
void gatt_svr_subscribe_cb(struct ble_gap_event *event);

#endif /* GATT_SVR_H */
