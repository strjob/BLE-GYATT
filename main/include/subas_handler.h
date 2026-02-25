/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Обработчик Subas протокола для BLE тестовой прошивки.
 * Формат: #TO/FROM/OP/DATA$
 */

#ifndef SUBAS_HANDLER_H
#define SUBAS_HANDLER_H

#include <stdint.h>

/* Макс. размер Subas сообщения (MTU 247 - 3 байта ATT header) */
#define SUBAS_MAX_MSG_LEN 244

/*
 * Обработать входящее Subas сообщение, сгенерировать ответ.
 *
 * conn_handle — BLE connection handle клиента, отправившего команду.
 * Используется для: RSSI измерения, привязки подписки к соединению.
 *
 * Возвращает длину ответа в output, 0 если ответ не нужен.
 */
uint16_t subas_handle_message(const uint8_t *input, uint16_t input_len,
                              uint8_t *output, uint16_t output_max_len,
                              uint16_t conn_handle);

#endif /* SUBAS_HANDLER_H */
