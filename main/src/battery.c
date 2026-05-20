/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Заряд батареи — mock-реализация для тестовой прошивки.
 * Всегда возвращает 100% (питание от USB/отладчика).
 */
#include "battery.h"

uint8_t battery_get_level(void) {
    return 100;
}
