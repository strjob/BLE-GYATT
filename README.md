| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | --------- | -------- | -------- | -------- |

# SOVA BLE Sensor

Прошивка BLE GATT-сервера для ESP32 на базе NimBLE. Реализует кастомный SOVA Service с двумя характеристиками (TX/RX) для обмена сообщениями по текстовому протоколу Subas. Поддерживает одновременное подключение нескольких Central-устройств (Multi-Central).

Разработана как BLE-транспорт для [sova-tauri](https://github.com/your-org/sova-tauri) — десктопного приложения для работы с датчиками.

## Сборка и прошивка

Требуется ESP-IDF toolchain. В `.devcontainer/` есть конфигурация для Docker-образа `espressif/idf`.

```bash
idf.py set-target esp32c6            # выбор чипа
idf.py build                         # сборка
idf.py -p <PORT> flash monitor       # прошивка + серийный монитор (Ctrl-] для выхода)
idf.py menuconfig                    # конфигурация (тип датчика, LED, GPIO, интервал)
```

## Архитектура

### Порядок инициализации

Точка входа — `app_main()` в `main/main.c`:

1. `led_init()` — настройка GPIO или LED strip
2. `nvs_flash_init()` — NVS (NimBLE хранит там bonding data)
3. `power_management_init()` — light sleep между BLE-событиями (опционально, через Kconfig)
4. `sensor_init()` — инициализация датчика (mock или DHT22, выбирается в Kconfig)
5. `nimble_port_init()` — запуск BLE-контроллера
6. `gap_init()` — GAP-сервис, временное имя устройства
7. `gatt_svc_init()` — регистрация GATT-сервиса с таблицей характеристик
8. `nimble_host_config_init()` — привязка callbacks, MTU 247
9. Запуск FreeRTOS-задач

### FreeRTOS-задачи

| Задача | Приоритет | Стек | Назначение |
|--------|-----------|------|------------|
| `nimble_host_task` | 5 | 4 KiB | Event loop NimBLE — обработка всех BLE-событий |
| `sensor_task` | 4 | 3 KiB | Периодическое чтение датчика и рассылка notify подписчикам |
| `status_led_task` | 3 | 2 KiB | LED-индикация: мигание (advertising) / горит (connected) |

`nimble_host_task` вызывает `nimble_port_run()` — блокирующий event loop, который диспатчит все GAP/GATT-события в зарегистрированные callbacks.

### Модули

```
main/
├── main.c                  — Entry point, инициализация, создание задач
├── include/
│   └── common.h            — UUID defines, shared includes, TAG
├── src/
│   ├── gap.c               — GAP: advertising, connection management
│   ├── gatt_svc.c          — GATT: SOVA Service, TX/RX характеристики, Multi-Central
│   ├── subas_handler.c     — Парсер Subas-протокола, маршрутизация команд
│   ├── sensor_mock.c       — Mock-датчик (градуальный дрифт T/H)
│   ├── sensor_dht22.c      — DHT22 (AM2302) через RMT backend
│   ├── sensor_task.c       — Периодическая рассылка данных подписчикам
│   └── led.c               — LED-абстракция (GPIO / addressable LED strip)
```

## BLE: GAP и Advertising

При синхронизации NimBLE-стека (`on_stack_sync`) вызывается `adv_init()`, которая:

1. Получает BLE MAC-адрес устройства
2. Формирует имя `SOVA-XXXX` (последние 2 байта MAC) для GAP Device Name
3. Запускает advertising

### Advertisement Packet (31 байт макс.)

| Поле | Размер | Значение |
|------|--------|----------|
| Flags | 3 B | `DISC_GEN \| BREDR_UNSUP` — General Discoverable, только BLE |
| 128-bit Service UUID | 18 B | SOVA Service UUID — для фильтрации при сканировании |
| Shortened Local Name | 2+N B | `CONFIG_SUBAS_DEVICE_NAME` (например `MOCK_TH`, `DHT22`) |

Shortened Local Name используется как device_type для discovery. Полное имя `SOVA-XXXX` доступно через GAP Device Name после подключения. Complete Local Name намеренно не включён в Scan Response — btleplug на Windows мержит ADV + Scan Response, и Complete Name перезатирает Shortened.

### Scan Response

Содержит только TX Power Level. Имя убрано по причине выше.

### Параметры

- Режим: `UND` (undirected connectable) — любое устройство может подключиться
- Интервал: 100–150 мс
- После подключения клиента advertising **перезапускается** для приёма следующих подключений (Multi-Central)

## BLE: GATT Service

### SOVA Service

| Элемент | UUID (128-bit) | Permissions |
|---------|----------------|-------------|
| **SOVA Sensor Service** | `33904903-971A-442F-803B-ABB332FCF9D2` | — |
| TX (App → Sensor) | `ECFC5128-3AE4-4A07-A46D-57423FD44703` | Write, Write Without Response |
| RX (Sensor → App) | `04B66E35-71D6-4E89-B43D-E83E2AB2CD29` | Notify |

UUID определены в `common.h` в little-endian (порядок NimBLE). Совпадают с `config/config.json` в sova-tauri.

### TX — приём команд

Callback `tx_chr_access()` срабатывает при записи в TX. Данные извлекаются из mbuf-цепочки, передаются в `subas_handle_message()`, ответ отправляется через `gatt_svc_notify_to()` конкретному клиенту по `conn_handle`.

### RX — отправка через Notify

RX-характеристика доступна только через Notify. Клиент должен подписаться, записав в CCCD (Client Characteristic Configuration Descriptor). При подписке NimBLE генерирует `BLE_GAP_EVENT_SUBSCRIBE`, обработчик `gatt_svr_subscribe_cb()` ставит флаг `notify_enabled` для данного клиента в таблице `s_clients[]`.

Две функции отправки:
- `gatt_svc_notify_to(conn_handle, data, len)` — конкретному клиенту (ответы на команды, периодические данные)
- `gatt_svc_notify_all(data, len)` — всем подписанным (broadcast)

Обе используют `ble_gatts_notify_custom()` с аллокацией `os_mbuf` под каждую отправку.

### Multi-Central

Таблица `s_clients[MAX_CLIENTS]` хранит состояние каждого подключения (`conn_handle`, `connected`, `notify_enabled`). `MAX_CLIENTS` берётся из `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`.

Потокобезопасность: `s_clients[]` модифицируется только из NimBLE Host Task (subscribe, add, remove). Чтение из Sensor Task в `gatt_svc_notify_to()` без блокировки — worst case: один пропущенный notify при race с disconnect.

## Протокол Subas

Текстовый протокол поверх BLE-характеристик. Формат сообщения:

```
#TO/FROM/OP$          — без данных
#TO/FROM/OP/DATA$     — с данными
```

Адресация по BLE MAC-адресу устройства (lowercase с двоеточиями: `aa:bb:cc:dd:ee:ff`).

### Команды

| Входящее сообщение | Ответ | Описание |
|--------------------|-------|----------|
| `#mac/APP/PING$` | `#APP/mac/PONG$` | Проверка связи |
| `#mac/APP/GET_INFO$` | `#APP/mac/INFO/fw/type/bat/interval/subscribed$` | Информация об устройстве |
| `#mac/APP/R$` | `#APP/mac/AD/T/H/RSSI$` | Одноразовое чтение датчика |
| `#mac/APP/W/ON$` | `#APP/mac/AM/ON$` | Старт периодической подписки |
| `#mac/APP/W/OFF$` | `#APP/mac/AM/OFF$` | Стоп подписки |
| `#mac/APP/W/Time=N$` | `#APP/mac/AM/Time=N$` | Установка интервала (мс, мин. 100) |
| `#mac/APP/W/DATA$` | `#APP/mac/AW/DATA$` | Acknowledge write |
| `#wrong_mac/APP/OP$` | `#APP/mac/NR$` | Not routed — адресат не совпадает |

Парсер (`subas_handler.c`) находит маркеры `#` и `$`, разбивает содержимое по `/` на поля TO, FROM, OP, DATA.

## Два режима отправки данных датчика

### По запросу (команда `R`)

Синхронная обработка внутри NimBLE Host Task:

```
TX write → tx_chr_access() → subas_handle_message() → sensor_read()
→ gatt_svc_notify_to(conn_handle) → клиент получает notify
```

Одно чтение — один ответ — конкретному клиенту, который запросил.

### Периодическая подписка (команда `W/ON`)

Задействует отдельную FreeRTOS-задачу `sensor_task`:

1. Команда `W/ON` из NimBLE Host Task вызывает `sensor_task_add_subscriber()`, которая записывает `{from, conn_handle}` в таблицу подписок `s_subs[]`
2. `sensor_task_fn()` в бесконечном цикле проверяет наличие подписчиков
3. Если есть — читает датчик один раз, затем для каждого подписчика формирует персональное AD-сообщение (с индивидуальным RSSI) и отправляет через `gatt_svc_notify_to()`
4. Засыпает на `s_interval_ms`, повторяет
5. Если подписчиков нет — спит 5 секунд (idle-режим для экономии CPU)

Потокобезопасность `s_subs[]`: доступ из двух задач (NimBLE Host Task и Sensor Task) защищён `portMUX_TYPE` spinlock.

### Интервал

`s_interval_ms` — **глобальный**, один на всех подписчиков. Инициализируется из `CONFIG_SENSOR_DEFAULT_INTERVAL_MS` (по умолчанию 1000 мс). Команда `W/Time=N` перезаписывает значение для всех. Если два клиента установят разные интервалы — побеждает последний.

Подписка автоматически удаляется при disconnect (`gap_event_handler` → `sensor_task_remove_subscriber()`).

## GAP Event Handler

Обработчик `gap_event_handler()` в `gap.c` обрабатывает:

| Событие | Действие |
|---------|----------|
| `CONNECT` | Добавить клиента в `s_clients[]`, запросить параметры соединения (interval 30-50ms, latency 4), перезапустить advertising |
| `DISCONNECT` | Удалить клиента, удалить подписку на sensor_task, перезапустить advertising |
| `SUBSCRIBE` | Делегировать в `gatt_svr_subscribe_cb()` для обновления `notify_enabled` |
| `CONN_UPDATE` | Логирование новых параметров соединения |
| `ADV_COMPLETE` | Перезапуск advertising |
| `NOTIFY_TX` | Логирование ошибок отправки notify |
| `MTU` | Логирование согласованного MTU |

## LED-индикация

- **Мигание** (200 мс вкл / 800 мс выкл) — advertising, ожидание подключения
- **Горит постоянно** — подключён хотя бы один клиент

Абстракция в `led.c` поддерживает два backend-а через Kconfig: GPIO (прямое управление пином) и addressable LED strip (через RMT или SPI).

## Конфигурация

### Kconfig (`idf.py menuconfig`)

- **Тип датчика**: Mock (генерация реалистичных данных с дрифтом) или DHT22 (AM2302 через RMT)
- **LED**: тип (GPIO / LED strip), номер GPIO-пина
- **Subas Device Name**: короткое имя для ADV (макс. 8 символов)
- **Интервал опроса**: по умолчанию 1000 мс, диапазон 100–60000 мс
- **Power Management**: light sleep, частоты CPU

### sdkconfig.defaults

BLE включён, NimBLE в качестве стека, MTU 247, GPIO LED pin 8.

## Лицензия

Unlicense OR CC0-1.0
