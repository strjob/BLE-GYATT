# План реализации: мониторинг батареи

## Текущее состояние

В `subas_handler.c` уровень заряда захардкожен как `100` в ответе на `GET_INFO`:

```c
"#%s/%s/INFO/%s/%s/100/%ld/%s$"
//                  ^^^ всегда 100
```

## Цель

Заменить хардкод на реальное чтение уровня заряда батареи через ADC. До появления железа — заглушка, возвращающая 100%.

## Аппаратная часть

### Схема подключения

```
VBAT (3.0–4.2V) ──┤R1 100K├──┬──┤R2 100K├── GND
                              │
                           GPIO (ADC)
```

Делитель 1:2, на входе ADC — 1.5–2.1V. Аттенюация ADC: 11dB (диапазон до ~2.5V на ESP32-C6).

### Выбор пина

Конкретный GPIO для ADC — через Kconfig (`CONFIG_BATTERY_ADC_CHANNEL`), по аналогии с `CONFIG_BLINK_GPIO`.

## Программная часть

### 1. Модуль `battery.c` / `battery.h`

Интерфейс:

```c
esp_err_t battery_init(void);       // инициализация ADC, калибровка
uint8_t   battery_get_percent(void); // 0–100%
uint16_t  battery_get_mv(void);      // напряжение в мВ (для отладки)
```

### 2. Реализация: выбор через Kconfig

```
CONFIG_BATTERY_TYPE_NONE   — заглушка (всегда 100%), для USB-питания и отладки
CONFIG_BATTERY_TYPE_ADC    — реальное чтение через ADC + делитель
```

По аналогии с `CONFIG_SENSOR_TYPE_MOCK` / `CONFIG_SENSOR_TYPE_DHT22`.

Файлы:
- `main/src/battery_none.c` — заглушка
- `main/src/battery_adc.c` — реальная реализация

### 3. ADC: чтение напряжения

ESP-IDF API:
- `esp_adc/adc_oneshot` — однократное чтение
- `esp_adc/adc_cali` — калибровка (компенсация разброса между чипами)

```c
// Инициализация (один раз)
adc_oneshot_unit_handle_t adc_handle;
adc_oneshot_unit_init_cfg_t cfg = { .unit_id = ADC_UNIT_1 };
adc_oneshot_new_unit(&cfg, &adc_handle);

adc_oneshot_chan_cfg_t chan_cfg = {
    .atten = ADC_ATTEN_DB_11,
    .bitwidth = ADC_BITWIDTH_12,
};
adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &chan_cfg);

// Калибровка
adc_cali_handle_t cali_handle;
adc_cali_curve_fitting_config_t cali_cfg = {
    .unit_id = ADC_UNIT_1,
    .atten = ADC_ATTEN_DB_11,
    .bitwidth = ADC_BITWIDTH_12,
};
adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);

// Чтение
int raw, voltage_mv;
adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw);
adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv);
uint16_t battery_mv = voltage_mv * 2;  // делитель 1:2
```

### 4. Напряжение → проценты: кусочно-линейная таблица

Кривая разряда LiPo 3.7V:

```c
static const struct { uint16_t mv; uint8_t pct; } lipo_table[] = {
    {4200, 100},
    {4060,  90},
    {3980,  80},
    {3920,  70},
    {3870,  60},
    {3820,  50},
    {3780,  40},
    {3700,  30},
    {3620,  20},
    {3500,  10},
    {3300,   0},
};
```

Интерполяция между соседними точками:

```c
uint8_t voltage_to_percent(uint16_t mv) {
    // Выше максимума
    if (mv >= lipo_table[0].mv)
        return 100;

    // Ниже минимума
    int last = sizeof(lipo_table) / sizeof(lipo_table[0]) - 1;
    if (mv <= lipo_table[last].mv)
        return 0;

    // Поиск интервала и линейная интерполяция
    for (int i = 0; i < last; i++) {
        if (mv >= lipo_table[i + 1].mv) {
            uint16_t mv_range  = lipo_table[i].mv  - lipo_table[i + 1].mv;
            uint8_t  pct_range = lipo_table[i].pct  - lipo_table[i + 1].pct;
            uint16_t mv_above  = mv - lipo_table[i + 1].mv;
            return lipo_table[i + 1].pct + (mv_above * pct_range) / mv_range;
        }
    }
    return 0;
}
```

### 5. Сглаживание

АЦП шумит. Скользящее среднее из последних N замеров (N=8–16):

```c
#define AVG_SAMPLES 8
static uint16_t samples[AVG_SAMPLES];
static int sample_idx = 0;

static uint16_t smoothed_mv(uint16_t new_mv) {
    samples[sample_idx] = new_mv;
    sample_idx = (sample_idx + 1) % AVG_SAMPLES;
    uint32_t sum = 0;
    for (int i = 0; i < AVG_SAMPLES; i++) sum += samples[i];
    return sum / AVG_SAMPLES;
}
```

### 6. Частота чтения

Батарея меняется медленно. Читать раз в 30–60 секунд, кешировать значение. Не привязывать к `sensor_task` — заряд батареи не зависит от подписки на данные датчика. Варианты:

- **esp_timer** (periodic, 30 сек) — легковесно, без отдельной задачи
- Чтение лениво при вызове `battery_get_percent()`, если прошло >30 сек с последнего замера

Второй вариант проще, не тратит ресурсы если никто не спрашивает.

## Интеграция в Subas

Единственное изменение — `subas_handler.c`, заменить `100` на `battery_get_percent()`:

```c
written = snprintf((char *)output, output_max_len,
                   "#%s/%s/INFO/%s/%s/%d/%ld/%s$",
                   from, gap_get_own_mac(),
                   FW_VERSION, sensor_get_type(),
                   battery_get_percent(),        // <-- было 100
                   (long)sensor_task_get_interval(),
                   sensor_task_is_subscribed() ? "1" : "0");
```

## Kconfig

Добавить в `main/Kconfig.projbuild`:

```
menu "Battery Monitor"

    choice BATTERY_TYPE
        prompt "Battery monitor type"
        default BATTERY_TYPE_NONE

        config BATTERY_TYPE_NONE
            bool "None (always 100%)"
            help
                Stub implementation, returns 100%. For USB-powered boards.

        config BATTERY_TYPE_ADC
            bool "ADC with voltage divider"
            help
                Read battery voltage through a resistor divider connected to ADC.

    endchoice

    config BATTERY_ADC_CHANNEL
        int "ADC channel number"
        depends on BATTERY_TYPE_ADC
        default 0
        range 0 6

    config BATTERY_DIVIDER_RATIO
        int "Voltage divider ratio x100 (e.g. 200 = 1:2)"
        depends on BATTERY_TYPE_ADC
        default 200

endmenu
```

## Порядок реализации

1. `battery.h` — интерфейс
2. `battery_none.c` — заглушка (return 100)
3. Интеграция в `subas_handler.c` — заменить хардкод
4. Kconfig — секция Battery Monitor
5. `battery_adc.c` — реальная реализация (когда появится железо)
6. Тестирование с мультиметром, подбор таблицы под конкретный аккумулятор
