/*
 * SPDX-FileCopyrightText: 2024 SOVA Project
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * Обработчик Subas протокола.
 * Формат сообщения: #TO/FROM/OP/DATA$ или #TO/FROM/OP$
 *
 * Поддерживаемые операции:
 *   PING      -> ответ PONG
 *   GET_INFO  -> ответ INFO с JSON (fw, type, bat)
 *   R         -> AR с тестовыми данными (acknowledge read)
 *   W         -> AW (acknowledge write)
 *   *         -> A с echo DATA (acknowledge)
 */
#include "subas_handler.h"
#include "common.h"

#define FIELD_MAX_LEN 64
#define DATA_MAX_LEN  128

/* Версия прошивки */
#define FW_VERSION "1.0.0"
#define SENSOR_TYPE "mock_ble"

/*
 * Парсинг Subas сообщения: #TO/FROM/OP/DATA$ или #TO/FROM/OP$
 * Минимум 2 слеша (TO, FROM, OP обязательны), третий опционален.
 * DATA — всё после третьего слеша до $, пустая строка если слеша нет.
 */
static bool parse_subas(const char *msg, uint16_t len,
                        char *to, char *from, char *op, char *data) {
    /* Найти маркеры начала и конца */
    const char *start = memchr(msg, '#', len);
    if (!start) {
        return false;
    }
    uint16_t remaining = len - (uint16_t)(start - msg);
    const char *end = memchr(start, '$', remaining);
    if (!end || end <= start + 1) {
        return false;
    }

    /* Пропустить # */
    start++;

    /* Найти первые 3 слеша */
    const char *slashes[3] = {NULL, NULL, NULL};
    int slash_count = 0;
    for (const char *p = start; p < end && slash_count < 3; p++) {
        if (*p == '/') {
            slashes[slash_count++] = p;
        }
    }

    /* Минимум 2 слеша (TO/FROM/OP), третий опционален (DATA) */
    if (slash_count < 2) {
        return false;
    }

    /* TO: start..slashes[0] */
    size_t flen = (size_t)(slashes[0] - start);
    if (flen >= FIELD_MAX_LEN) {
        return false;
    }
    memcpy(to, start, flen);
    to[flen] = '\0';

    /* FROM: slashes[0]+1..slashes[1] */
    flen = (size_t)(slashes[1] - slashes[0] - 1);
    if (flen >= FIELD_MAX_LEN) {
        return false;
    }
    memcpy(from, slashes[0] + 1, flen);
    from[flen] = '\0';

    if (slash_count == 3) {
        /* OP: slashes[1]+1..slashes[2] */
        flen = (size_t)(slashes[2] - slashes[1] - 1);
        if (flen >= FIELD_MAX_LEN) {
            return false;
        }
        memcpy(op, slashes[1] + 1, flen);
        op[flen] = '\0';

        /* DATA: slashes[2]+1..end (может быть пустым) */
        flen = (size_t)(end - slashes[2] - 1);
        if (flen >= DATA_MAX_LEN) {
            flen = DATA_MAX_LEN - 1;
        }
        memcpy(data, slashes[2] + 1, flen);
        data[flen] = '\0';
    } else {
        /* Нет третьего слеша: OP — всё от slashes[1]+1 до $, DATA пустой */
        flen = (size_t)(end - slashes[1] - 1);
        if (flen >= FIELD_MAX_LEN) {
            return false;
        }
        memcpy(op, slashes[1] + 1, flen);
        op[flen] = '\0';

        data[0] = '\0';
    }

    return true;
}

uint16_t subas_handle_message(const uint8_t *input, uint16_t input_len,
                              uint8_t *output, uint16_t output_max_len) {
    char to[FIELD_MAX_LEN] = {0};
    char from[FIELD_MAX_LEN] = {0};
    char op[FIELD_MAX_LEN] = {0};
    char data[DATA_MAX_LEN] = {0};

    if (!parse_subas((const char *)input, input_len, to, from, op, data)) {
        ESP_LOGW(TAG, "невалидное Subas сообщение: %.*s", input_len,
                 (const char *)input);
        return 0;
    }

    ESP_LOGI(TAG, data[0] ? "RX: #%s/%s/%s/%s$" : "RX: #%s/%s/%s$",
             to, from, op, data);

    int written = 0;

    /* Ответ на PING */
    if (strcmp(op, "PING") == 0) {
        written = snprintf((char *)output, output_max_len,
                           "#%s/%s/PONG$", from, to);
    }
    /* Ответ на GET_INFO — информация об устройстве */
    else if (strcmp(op, "GET_INFO") == 0) {
        written = snprintf((char *)output, output_max_len,
                           "#%s/%s/INFO/"
                           "{\"fw\":\"%s\",\"type\":\"%s\",\"bat\":100}$",
                           from, to, FW_VERSION, SENSOR_TYPE);
    }
    /* R (read) — ответ AR с тестовыми данными */
    else if (strcmp(op, "R") == 0) {
        written = snprintf((char *)output, output_max_len,
                           "#%s/%s/AR/%s$", from, to,
                           data[0] ? data : "mock_ble,25.3");
    }
    /* W (write) — ответ AW (подтверждение записи) */
    else if (strcmp(op, "W") == 0) {
        written = snprintf((char *)output, output_max_len,
                           data[0] ? "#%s/%s/AW/%s$" : "#%s/%s/AW$",
                           from, to, data);
    }
    /* Всё остальное — ответ A с echo DATA */
    else {
        written = snprintf((char *)output, output_max_len,
                           "#%s/%s/A/%s$", from, to,
                           data[0] ? data : op);
    }

    if (written > 0 && written < (int)output_max_len) {
        ESP_LOGI(TAG, "TX: %.*s", written, (char *)output);
        return (uint16_t)written;
    }

    ESP_LOGW(TAG, "ответ не поместился в буфер (%d >= %d)", written,
             output_max_len);
    return 0;
}
