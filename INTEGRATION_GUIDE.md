# SOVA BLE Sensor — Руководство по интеграции

Это руководство для разработчиков, которые пишут клиентское приложение (ПК или Android) для работы с датчиками SOVA на ESP32.

---

## Оглавление

1. [UUIDs и константы](#1-uuids-и-константы)
2. [Обнаружение датчиков (Scan)](#2-обнаружение-датчиков-scan)
3. [Подключение и GATT Discovery](#3-подключение-и-gatt-discovery)
4. [Подписка на уведомления (Notify)](#4-подписка-на-уведомления-notify)
5. [Протокол Subas](#5-протокол-subas)
6. [Команды и ответы](#6-команды-и-ответы)
7. [Режимы получения данных](#7-режимы-получения-данных)
8. [Полный flow от скана до данных](#8-полный-flow-от-скана-до-данных)
9. [Формат данных датчика](#9-формат-данных-датчика)
10. [Отключение и переподключение](#10-отключение-и-переподключение)
11. [Советы для Android](#11-советы-для-android)

---

## 1. UUIDs и константы

Прошивка использует кастомный 128-битный GATT Service с двумя характеристиками:

| Элемент | UUID |
|---------|------|
| **SOVA Sensor Service** | `33904903-971a-442f-803b-abb332fcf9d2` |
| **TX** (App → Датчик, Write) | `ecfc5128-3ae4-4a07-a46d-57423fd44703` |
| **RX** (Датчик → App, Notify) | `04b66e35-71d6-4e89-b43d-e83e2ab2cd29` |

- **TX** — характеристика для отправки команд датчику (Write / Write Without Response).
- **RX** — характеристика для получения ответов и периодических данных через Notify. Запись напрямую невозможна.

> UUID в прошивке хранятся в little-endian (NimBLE-формат), но в advertisement пакете и GATT Discovery они приходят в стандартном big-endian виде. Используйте строки выше в том виде, как они записаны — большинство BLE-библиотек принимают оба формата.

---

## 2. Обнаружение датчиков (Scan)

### Как прошивка рекламирует себя

Датчик отправляет Advertisement Packet (ADV_IND, undirected connectable) со следующим содержимым:

| Поле | Значение |
|------|----------|
| Flags | General Discoverable, BR/EDR Not Supported |
| 128-bit Service UUID | `33904903-971a-442f-803b-abb332fcf9d2` |
| Shortened Local Name | тип устройства: `DHT22`, `DHT22_T`, `DHT22_H`, `MOCK_TH`, `MOCK_T`, `MOCK_H` |

**Scan Response** содержит только TX Power Level. Полное имя (`SOVA-XXXX`) доступно только после подключения через GAP Device Name, не из Scan Response.

> На Windows через btleplug ADV и Scan Response мержатся — не полагайтесь на Complete Local Name из Scan Response для определения типа устройства, используйте Shortened Local Name из ADV.

### Как сканировать

Фильтруйте по `service_uuid` — это самый надёжный способ найти только датчики SOVA:

```
ScanFilter: service_uuid = "33904903-971a-442f-803b-abb332fcf9d2"
```

Без фильтра вы получите все BLE-устройства в радиусе. Фильтрация по UUID работает на уровне ОС и экономит батарею.

### Что вы получаете из scan-результатов

После скана для каждого найденного устройства доступно:

- **MAC-адрес** — идентификатор устройства, например `AA:BB:CC:DD:EE:FF`
- **Shortened Local Name** — тип датчика из поля рекламы (`DHT22`, `MOCK_TH` и т.д.)
- **RSSI** — уровень сигнала в дБм
- **Service UUID** — подтверждает, что это датчик SOVA

### MAC-адрес как идентификатор

MAC-адрес — основной идентификатор датчика. Используйте его для:
- подключения к конкретному устройству
- сохранения в конфиге (привязка имени к датчику)
- адресации в протоколе Subas (поле `TO` в командах)

Нормализуйте MAC в lowercase с двоеточиями: `aa:bb:cc:dd:ee:ff`.

> На iOS/macOS из-за политики приватности CoreBluetooth не даёт прямой доступ к MAC-адресу. Вместо него используется UUID периферии, генерируемый ОС. Этот UUID не постоянный между сессиями на чужих устройствах, но постоянный для одного iPhone/Mac. Учитывайте это при сохранении конфига.

---

## 3. Подключение и GATT Discovery

После получения MAC-адреса из скана:

1. **Подключитесь** к устройству по MAC.
2. **Выполните GATT Discovery** — запрос списка сервисов и характеристик.
3. **Найдите** SOVA Service по UUID `33904903-971a-442f-803b-abb332fcf9d2`.
4. **Получите ссылки** на TX и RX характеристики по их UUID.

После подключения прошивка:
- Добавляет клиента в таблицу `s_clients[]`
- Запрашивает обновление параметров соединения: интервал 30–50 мс, latency 4
- Перезапускает advertising для приёма следующих подключений (Multi-Central поддерживается)

### MTU

Прошивка поддерживает MTU 247 байт. Инициируйте MTU exchange после подключения — это позволит передавать длинные сообщения без фрагментации.

---

## 4. Подписка на уведомления (Notify)

RX-характеристика работает только через Notify. Данные датчик **не отправляет сам по себе** — нужна явная подписка.

**Как подписаться:**

Запишите `0x0001` в CCCD (Client Characteristic Configuration Descriptor) RX-характеристики. Большинство BLE-библиотек делают это через метод `subscribe(characteristic)` или `enableNotify()`.

После записи в CCCD прошивка генерирует `BLE_GAP_EVENT_SUBSCRIBE` и устанавливает флаг `notify_enabled` для вашего подключения. Только после этого датчик начнёт отправлять вам данные.

> Без подписки на Notify ответы на команды и периодические данные вы не получите.

---

## 5. Протокол Subas

Все команды и ответы передаются как текстовые строки в UTF-8.

### Формат сообщения

```
#TO/FROM/OP$           — без данных
#TO/FROM/OP/DATA$      — с данными
```

| Поле | Описание |
|------|----------|
| `TO` | Адресат — MAC датчика (`aa:bb:cc:dd:ee:ff`) или `APP` |
| `FROM` | Отправитель — `APP` для команд с вашей стороны |
| `OP` | Операция (см. таблицу команд) |
| `DATA` | Опциональные данные |

**Разделители:** `#` — начало, `$` — конец, `/` — разделитель полей.

### Адресация

- Команды от приложения → датчику: поле `TO` = MAC датчика в lowercase с двоеточиями
- Ответы от датчика → приложению: поле `TO` = `APP`, поле `FROM` = MAC датчика

**Пример команды:**
```
#aa:bb:cc:dd:ee:ff/APP/PING$
```

**Пример ответа:**
```
#APP/aa:bb:cc:dd:ee:ff/PONG$
```

---

## 6. Команды и ответы

| Команда | Ответ | Описание |
|---------|-------|----------|
| `#mac/APP/PING$` | `#APP/mac/PONG$` | Проверка связи |
| `#mac/APP/GET_INFO$` | `#APP/mac/INFO/fw/type/bat/interval/subscribed$` | Информация об устройстве |
| `#mac/APP/R$` | `#APP/mac/AD/...данные...$` | Одноразовое чтение датчика |
| `#mac/APP/W/ON$` | `#APP/mac/AW/ON$` | Старт периодической рассылки |
| `#mac/APP/W/OFF$` | `#APP/mac/AW/OFF$` | Стоп периодической рассылки |
| `#mac/APP/W/Time=N$` | `#APP/mac/AW/Time=N$` | Установить интервал N мс (мин. 100) |
| `#wrong_mac/APP/OP$` | `#APP/mac/NR$` | Not Routed — MAC не совпал |

### Ответ GET_INFO

```
#APP/mac/INFO/fw/type/bat/interval/subscribed$
```

| Поле | Пример | Описание |
|------|--------|----------|
| `fw` | `1.0.0` | Версия прошивки |
| `type` | `DHT22` | Тип датчика (совпадает с Shortened Local Name) |
| `bat` | `100` | Заряд батареи в % |
| `interval` | `1000` | Текущий интервал рассылки в мс |
| `subscribed` | `1` | 1 если вы подписаны на периодические данные, 0 если нет |

### Ошибочные ответы

| Ответ | Описание |
|-------|----------|
| `#APP/mac/NR$` | Not Routed — MAC в команде не совпал с MAC датчика |

---

## 7. Режимы получения данных

### Режим 1: По запросу (команда `R`)

Отправьте `#mac/APP/R$` — датчик немедленно считает и вернёт один пакет данных.

Используйте когда нужно одно актуальное значение без постоянного подключения.

### Режим 2: Периодическая подписка (команды `W/ON`, `W/OFF`)

1. Отправьте `#mac/APP/W/ON$` — датчик начинает рассылать данные с текущим интервалом
2. Данные приходят через Notify на RX характеристику с заданной периодичностью
3. Отправьте `#mac/APP/W/OFF$` — рассылка останавливается

**Установка интервала:**
```
#mac/APP/W/Time=2000$   — рассылка каждые 2 секунды
```
Минимальный интервал — 100 мс. Интервал глобальный: если несколько клиентов подключены одновременно, побеждает последняя установка.

**Подписка автоматически снимается** при BLE-отключении — не нужно отдельно отправлять `W/OFF` при разрыве соединения.

---

## 8. Полный flow от скана до данных

```
┌─────────────────────────────────────────────────────┐
│                                                     │
│  1. BLE SCAN                                        │
│     Фильтр по Service UUID:                         │
│     33904903-971a-442f-803b-abb332fcf9d2            │
│     → Получаем список MAC + Shortened Local Name    │
│                                                     │
│  2. CONNECT                                         │
│     Подключаемся к MAC конкретного датчика          │
│                                                     │
│  3. MTU EXCHANGE                                    │
│     Запрашиваем MTU 247 (или больше если поддерж.)  │
│                                                     │
│  4. GATT DISCOVERY                                  │
│     Ищем SOVA Service UUID                          │
│     → Получаем handle TX char (Write)               │
│     → Получаем handle RX char (Notify)              │
│                                                     │
│  5. SUBSCRIBE TO NOTIFY                             │
│     Пишем 0x0001 в CCCD RX-характеристики           │
│                                                     │
│  6. PING (опционально)                              │
│     Пишем в TX: #mac/APP/PING$                      │
│     Получаем по Notify: #APP/mac/PONG$              │
│                                                     │
│  7. GET_INFO (опционально)                          │
│     Пишем в TX: #mac/APP/GET_INFO$                  │
│     Получаем: #APP/mac/INFO/fw/type/bat/...         │
│                                                     │
│  8a. ОДНОРАЗОВОЕ ЧТЕНИЕ                             │
│     Пишем в TX: #mac/APP/R$                         │
│     Получаем: #APP/mac/AD/...данные...$             │
│                                                     │
│  8b. ПЕРИОДИЧЕСКИЕ ДАННЫЕ                           │
│     Пишем в TX: #mac/APP/W/ON$                      │
│     Получаем: #APP/mac/AW/ON$  (подтверждение)      │
│     Далее автоматически: #APP/mac/AD/...$ каждые N мс│
│     Для остановки: #mac/APP/W/OFF$                  │
│                                                     │
│  9. DISCONNECT                                      │
│     Прошивка снимает подписку, перезапускает ADV    │
│                                                     │
└─────────────────────────────────────────────────────┘
```

---

## 9. Формат данных датчика

Ответ на команду `R` и каждый периодический notify (режим `W/ON`) имеет одинаковый формат:

```
#APP/mac/AD/...поля.../rssi/bat$
```

Поля зависят от конфигурации датчика (настраивается в прошивке):

| Конфигурация | Формат |
|--------------|--------|
| Temperature + Humidity | `#APP/mac/AD/temp/hum/rssi/bat$` |
| Только Temperature | `#APP/mac/AD/temp/rssi/bat$` |
| Только Humidity | `#APP/mac/AD/hum/rssi/bat$` |

**Примеры:**
```
#APP/aa:bb:cc:dd:ee:ff/AD/22.5/63.1/-58/100$
```
→ температура 22.5 °C, влажность 63.1 %, RSSI -58 дБм, батарея 100 %

```
#APP/aa:bb:cc:dd:ee:ff/AD/22.5/-58/100$
```
→ только температура (режим T)

### Как определить конфигурацию датчика

Тип вывода кодируется в Shortened Local Name из Advertisement:

| Shortened Local Name | Описание |
|----------------------|----------|
| `DHT22` | DHT22, Temperature + Humidity |
| `DHT22_T` | DHT22, только Temperature |
| `DHT22_H` | DHT22, только Humidity |
| `MOCK_TH` | Mock-датчик, Temperature + Humidity |
| `MOCK_T` | Mock-датчик, только Temperature |
| `MOCK_H` | Mock-датчик, только Humidity |

По суффиксу `_T` / `_H` / отсутствию суффикса определяйте, сколько полей данных ожидать в AD-пакете.

Также это значение возвращается в поле `type` ответа на `GET_INFO`.

### Единицы измерения

| Поле | Единица | Диапазон (DHT22) |
|------|---------|-----------------|
| `temp` | °C | −40 … +80 |
| `hum` | % | 0 … 100 |
| `rssi` | дБм | обычно −100 … 0 |
| `bat` | % | 0 … 100 |

Десятичный разделитель — точка. Значения приходят как строки, парсите через `parseFloat` / `Float.parseFloat`.

---

## 10. Отключение и переподключение

- При `DISCONNECT` прошивка удаляет клиента из таблицы и снимает подписку на периодические данные.
- Advertising автоматически перезапускается — датчик снова виден для скана.
- При переподключении нужно заново подписаться на Notify (CCCD не сохраняется без bonding).

**Bonding (хранение ключей):**
Прошивка поддерживает NimBLE bonding (хранит данные в NVS). Если вы установили bonding, CCCD может сохраниться между сессиями. Но рассчитывать на это не стоит — всегда подписывайтесь заново при подключении.

---

## 11. Советы для Android

### Требуемые разрешения (AndroidManifest.xml)

```xml
<!-- Android 12+ -->
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />

<!-- Android 11 и ниже -->
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
```

Запрашивайте разрешения в runtime перед началом скана.

### Стандартные проблемы и решения

**GATT Discovery возвращает пустой список на Android:**
Добавьте задержку 600 мс после `onConnectionStateChange(CONNECTED)` перед вызовом `discoverServices()`. Android BLE стек нестабилен при немедленном Discovery.

**Notify не приходят:**
Убедитесь, что вы пишете в CCCD именно через `writeDescriptor()`, а не игнорируете этот шаг. На Android вызов `setCharacteristicNotification(true)` на объекте характеристики — это только локальный флаг, без записи в CCCD датчик не знает о подписке.

**Соединение разрывается через 30–60 секунд без активности:**
Прошивка запрашивает параметры соединения с latency 4 (connection latency). Это нормально и экономит батарею. Если OС Android разрывает соединение — уменьшите latency на стороне клиента через `requestConnectionPriority(HIGH)`.

**Scan не находит устройства:**
- Убедитесь, что Bluetooth включён и разрешения выданы
- На Android 7+ scan автоматически останавливается через 30 минут — перезапускайте
- Используйте фильтрацию по Service UUID, а не по имени устройства

### Рекомендуемые библиотеки

| Платформа | Библиотека |
|-----------|------------|
| Android (Kotlin/Java) | [Nordic Android BLE Library](https://github.com/NordicSemiconductor/Android-BLE-Library) |
| Flutter | `flutter_blue_plus` |
| React Native | `react-native-ble-plx` |
| Kotlin Multiplatform | `Kable` |
| Rust (Desktop) | `btleplug` |
| Python (Desktop) | `bleak` |

---

## Пример минимальной сессии (псевдокод)

```
// 1. Сканируем
devices = ble.scan(filter: service_uuid="33904903-971a-442f-803b-abb332fcf9d2", duration=5s)

// 2. Выбираем датчик
device = devices.first  // { mac: "aa:bb:cc:dd:ee:ff", name: "DHT22", rssi: -55 }

// 3. Подключаемся
conn = ble.connect(device.mac)
conn.requestMtu(247)

// 4. Находим характеристики
service = conn.getService("33904903-971a-442f-803b-abb332fcf9d2")
tx = service.getCharacteristic("ecfc5128-3ae4-4a07-a46d-57423fd44703")
rx = service.getCharacteristic("04b66e35-71d6-4e89-b43d-e83e2ab2cd29")

// 5. Подписываемся на Notify
conn.subscribe(rx)  // записывает 0x0001 в CCCD

// 6. Подписываемся на события
rx.onNotify = (data) => {
    msg = data.toString("utf8")  // "#APP/aa:bb:cc:dd:ee:ff/AD/22.5/63.1/-58/100$"
    parsed = subas_parse(msg)
    print(parsed.temp, parsed.hum)
}

// 7. Запрашиваем данные
mac = "aa:bb:cc:dd:ee:ff"
tx.write("#" + mac + "/APP/R$")

// 8. Запускаем периодику
tx.write("#" + mac + "/APP/W/Time=2000$")
tx.write("#" + mac + "/APP/W/ON$")

// ... получаем данные каждые 2 секунды ...

// 9. Останавливаем и отключаемся
tx.write("#" + mac + "/APP/W/OFF$")
conn.disconnect()
```

---

## Структура парсера Subas (пример на Python)

```python
import re

def parse_subas(message: str) -> dict | None:
    # Ожидаем формат: #TO/FROM/OP$ или #TO/FROM/OP/DATA$
    m = re.match(r'^#([^/]+)/([^/]+)/([^/$]+)(?:/([^$]*))?\\$', message)
    if not m:
        return None
    return {
        "to":   m.group(1),
        "from": m.group(2),
        "op":   m.group(3),
        "data": m.group(4),  # None если нет
    }

def parse_ad(message: str, sensor_type: str) -> dict | None:
    parsed = parse_subas(message)
    if not parsed or parsed["op"] != "AD":
        return None

    parts = parsed["data"].split("/") if parsed["data"] else []
    mac = parsed["from"]

    if "_T" in sensor_type:
        temp, rssi, bat = parts
        return {"mac": mac, "temp": float(temp), "rssi": int(rssi), "bat": int(bat)}
    elif "_H" in sensor_type:
        hum, rssi, bat = parts
        return {"mac": mac, "hum": float(hum), "rssi": int(rssi), "bat": int(bat)}
    else:
        temp, hum, rssi, bat = parts
        return {"mac": mac, "temp": float(temp), "hum": float(hum),
                "rssi": int(rssi), "bat": int(bat)}
```

---

*Прошивка: [nimBLE_GATT_Server](https://github.com/your-org/nimBLE_GATT_Server) — Unlicense OR CC0-1.0*
