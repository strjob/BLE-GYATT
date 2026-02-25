/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * GAP — advertising и управление соединениями.
 */
#ifndef GAP_SVC_H
#define GAP_SVC_H

/* NimBLE GAP APIs */
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

/* Инициализировать advertising (вызывается при синхронизации стека) */
void adv_init(void);

/* Инициализировать GAP сервис и имя устройства */
int gap_init(void);

/* Проверить, подключён ли клиент */
bool gap_is_connected(void);

/* Получить собственный BLE MAC в формате "aa:bb:cc:dd:ee:ff" (lowercase с двоеточиями).
 * Валиден после вызова adv_init(). Используется как адрес в Subas протоколе. */
const char *gap_get_own_mac(void);

#endif /* GAP_SVC_H */
