# Общие исходники (без sensor_*.c — они выбираются по Kconfig)
file(GLOB srcs "main.c" "src/*.c")

# Условная компиляция реализации датчика
if(CONFIG_SENSOR_TYPE_DHT22)
    list(FILTER srcs EXCLUDE REGEX "sensor_mock\\.c$")
elseif(CONFIG_SENSOR_TYPE_MOCK)
    list(FILTER srcs EXCLUDE REGEX "sensor_dht22\\.c$")
endif()

idf_component_register(SRCS "${srcs}"
                       PRIV_REQUIRES bt nvs_flash esp_driver_gpio esp_pm
                       INCLUDE_DIRS "./include")
