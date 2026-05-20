/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Абстракция заряда батареи.
 */
#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

/* Уровень заряда батареи, 0–100 % */
uint8_t battery_get_level(void);

#endif /* BATTERY_H */
