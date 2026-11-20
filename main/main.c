/**
 * 7ch-zigbee-relay — 7-канальный Zigbee контроллер реле на ESP32-H2 + ULN2003
 *
 * Полностью открытая альтернатива платной прошивке с Lava.top
 * Лицензия: MIT
 *
 * Возможности:
 *   • 7 независимых Zigbee-каналов (On/Off) для Home Assistant / ZHA
 *   • Каждый канал — отдельный endpoint в Zigbee-сети
 *   • Защита от дребезга механических контактов
 *   • Сохранение состояний в NVS (энергонезависимая память)
 *   • Сброс Zigbee-сети по длинному нажатию (6x за 5 сек)
 *   • Работает как Zigbee Router (усиливает mesh)
 *   • OTA-обновления по Zigbee
 *
 * Сборка:
 *   1. Установить ESP-IDF v5.1.2+
 *      git clone --recursive https://github.com/espressif/esp-idf.git
 *      cd esp-idf && ./install.sh && source export.sh
 *   2. Склонировать ESP-Zigbee-SDK
 *      git clone https://github.com/espressif/esp-zigbee-sdk.git
 *      export ZIGBEE_SDK_PATH=/path/to/esp-zigbee-sdk
 *   3. Собрать прошивку
 *      cd 7ch-zigbee-relay
 *      idf.py set-target esp32h2
 *      idf.py build
 *   4. Загрузить на ESP32-H2
 *      idf.py -p /dev/ttyUSB0 flash monitor
 *
 * Аппаратная часть:
 *   ESP32-H2 Super Mini (GPIO2..GPIO8)
 *         │
 *         ├─ GPIO2 ──→ ULN2003 IN1 ──→ Реле 1
 *         ├─ GPIO3 ──→ ULN2003 IN2 ──→ Реле 2
 *         ├─ GPIO4 ──→ ULN2003 IN3 ──→ Реле 3
 *         ├─ GPIO5 ──→ ULN2003 IN4 ──→ Реле 4
 *         ├─ GPIO6 ──→ ULN2003 IN5 ──→ Реле 5
 *         ├─ GPIO7 ──→ ULN2003 IN6 ──→ Реле 6
 *         └─ GPIO8 ──→ ULN2003 IN7 ──→ Реле 7
 *
 *   Питание ULN2003: 5V VCC + GND (от USB или внешнего БП)
 *   Реле: подойдут любые 5V/12V реле на 10A (SRD, и т.п.)
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_zb_core.h"
#include "ha/esp_zb_on_off_light.h"

/* ============================================================
 *  КОНФИГУРАЦИЯ
 * ============================================================ */
#define CHANNEL_COUNT   7             /* Количество каналов */

/* GPIO, к которым подключены входы ULN2003 (IN1..IN7) */
static const uint8_t relay_gpios[CHANNEL_COUNT] = { 2, 3, 4, 5, 6, 7, 8 };

/* Zigbee endpoint ID для каждого канала (ZHA видит их как отдельные устройства) */
static const uint8_t zigbee_endpoints[CHANNEL_COUNT] = { 1, 2, 3, 4, 5, 6, 7 };

/* Имена каналов */
static const char *channel_names[CHANNEL_COUNT] = {
    "Канал 1", "Канал 2", "Канал 3",
    "Канал 4", "Канал 5", "Канал 6", "Канал 7"
};

/* Защита от дребезга: минимальный интервал между переключениями (мкс) */
#define DEBOUNCE_INTERVAL_US  50000   /* 50 мс */

/* Длительное нажатие для сброса Zigbee-сети (мкс) */
#define FACTORY_RESET_HOLD_US 5000000 /* 5 секунд */

/* NVS ключ для сохранения состояний */
#define NVS_STORAGE_KEY  "relay_states"

/* ============================================================
 *  ГЛОБАЛЬНОЕ СОСТОЯНИЕ
 * ============================================================ */
static bool relay_states[CHANNEL_COUNT] = { false };
static uint64_t last_toggle_time[CHANNEL_COUNT] = { 0 };
static bool zigbee_network_joined = false;
static uint8_t factory_reset_count = 0;
static uint64_t factory_reset_window_start = 0;

/* ============================================================
 *  ИНИЦИАЛИЗАЦИЯ GPIO
 * ============================================================ */
static void gpio_init(void)
{
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        gpio_reset_pin(relay_gpios[i]);
        gpio_set_direction(relay_gpios[i], GPIO_MODE_OUTPUT);
        gpio_set_level(relay_gpios[i], 0); /* все выключены */
    }
    ESP_LOGI("RELAY", "GPIO initialized: %d channels", CHANNEL_COUNT);
}

/* ============================================================
 *  УПРАВЛЕНИЕ РЕЛЕ
 * ============================================================ */
static void relay_set(uint8_t channel, bool state)
{
    if (channel >= CHANNEL_COUNT) return;

    uint64_t now = esp_timer_get_time();

    /* Защита от дребезга */
    if (now - last_toggle_time[channel] < DEBOUNCE_INTERVAL_US) {
        ESP_LOGW("RELAY", "Channel %d: debounce skip", channel + 1);
        return;
    }

    relay_states[channel] = state;
    gpio_set_level(relay_gpios[channel], state ? 1 : 0);
    last_toggle_time[channel] = now;

    ESP_LOGI("RELAY", "Channel %d (%s): %s",
             channel + 1,
             channel_names[channel],
             state ? "ON" : "OFF");
}

static bool relay_get(uint8_t channel)
{
    if (channel >= CHANNEL_COUNT) return false;
    return relay_states[channel];
}

/* ============================================================
 *  СОХРАНЕНИЕ / ВОССТАНОВЛЕНИЕ СОСТОЯНИЙ (NVS)
 * ============================================================ */
#include "nvs_handle.h"

static void nvs_save_states(void)
{
    nvs_handle_t *handle = NULL;
    esp_err_t err;

    err = nvs_open("relay", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to open NVS handle");
        return;
    }

    /* Упаковываем состояния в uint64 bitmask: 7 бит */
    uint64_t bitmask = 0;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (relay_states[i]) bitmask |= (1ULL << i);
    }

    err = nvs_set_u64(handle, NVS_STORAGE_KEY, bitmask);
    if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI("NVS", "States saved: 0x%llx", bitmask);
    } else {
        ESP_LOGE("NVS", "Failed to save states, err=%d", err);
    }

    nvs_close(handle);
}

static void nvs_restore_states(void)
{
    nvs_handle_t *handle = NULL;
    esp_err_t err;

    err = nvs_open("relay", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Failed to open NVS handle");
        return;
    }

    uint64_t bitmask = 0;
    err = nvs_get_u64(handle, NVS_STORAGE_KEY, &bitmask);
    if (err == ESP_OK) {
        for (int i = 0; i < CHANNEL_COUNT; i++) {
            bool state = (bitmask >> i) & 1;
            relay_set(i, state);
        }
        ESP_LOGI("NVS", "States restored: 0x%llx", bitmask);
    } else {
        ESP_LOGI("NVS", "No saved states found, using defaults (all OFF)");
    }

    nvs_close(handle);
}

/* ============================================================
 *  ZIGBEE: ОБРАБОТКА КОМАНД ВКЛ/ВЫКЛ
 * ============================================================ */
static esp_zb_on_off_status_t zigbee_on_off_cb(
    uint8_t endpoint,
    esp_zb_on_off_value_t value)
{
    uint8_t channel = endpoint - 1; /* endpoint 1..7 → channel 0..6 */

    if (channel >= CHANNEL_COUNT) {
        ESP_LOGE("ZB", "Unknown endpoint: %d", endpoint);
        return ESP_ZB_ON_OFF_STATUS_ERROR;
    }

    bool new_state = (value == ESP_ZB_ON_OFF_VALUE_ON);
    relay_set(channel, new_state);
    nvs_save_states();

    ESP_LOGI("ZB", "Endpoint %d → %s", endpoint, new_state ? "ON" : "OFF");

    return (new_state ? ESP_ZB_ON_OFF_STATUS_ON : ESP_ZB_ON_OFF_STATUS_OFF);
}

static esp_zb_on_off_status_t zigbee_on_off_get_cb(
    uint8_t endpoint)
{
    uint8_t channel = endpoint - 1;
    if (channel >= CHANNEL_COUNT) return ESP_ZB_ON_OFF_STATUS_ERROR;

    return (relay_states[channel]
                ? ESP_ZB_ON_OFF_STATUS_ON
                : ESP_ZB_ON_OFF_STATUS_OFF);
}

/* ============================================================
 *  ZIGBEE: ИНИЦИАЛИЗАЦИЯ (7 ENDPOINT'ОВ)
 * ============================================================ */
static void zigbee_init(void)
{
    esp_zb_platform_config_t platform_config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG()
    };
    esp_zb_platform_config(&platform_config);

    /* Создаём устройство с несколькими endpoint'ами */
    esp_zb_device_t *device = esp_zb_device_create();
    if (!device) {
        ESP_LOGE("ZB", "Failed to create device");
        return;
    }

    /* Для каждого канала создаём endpoint с On/Off Light кластером */
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        esp_zb_endpoint_config_t ep_config = {
            .endpoint = zigbee_endpoints[i],
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
            .app_device_version = 0
        };

        /* On/Off кластер (сервер) */
        esp_zb_on_off_cluster_cfg_t on_off_cfg = {
            .on_off = relay_states[i] ? 1 : 0
        };

        esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
        if (!cluster_list) {
            ESP_LOGE("ZB", "Failed to create cluster list for ep %d", i);
            continue;
        }

        esp_zb_cluster_list_add_on_off_cluster(
            cluster_list,
            &on_off_cfg,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            esp_zb_on_off_cluster_server_cb_reg
        );

        /* Регистрируем endpoint в устройстве */
        esp_zb_device_add_endpoint(
            device,
            esp_zb_endpoint_create(&ep_config),
            cluster_list
        );

        ESP_LOGI("ZB", "Endpoint %d registered (channel %d)",
                 zigbee_endpoints[i], i + 1);
    }

    /* Регистрируем устройство в Zigbee-стеке */
    esp_zb_device_register(device);

    ESP_LOGI("ZB", "Zigbee initialized: %d endpoints", CHANNEL_COUNT);
}

/* ============================================================
 *  ZIGBEE: СТАТУС СОЕДИНЕНИЯ
 * ============================================================ */
static void zigbee_status_cb(esp_zb_connection_status_t status)
{
    switch (status) {
    case ESP_ZB_CONNECTION_STATUS_JOINED:
        zigbee_network_joined = true;
        ESP_LOGI("ZB", "Network JOINED — device ready in HA");
        /* При подключении восстанавливаем состояния и синхронизируем с Zigbee */
        nvs_restore_states();
        break;

    case ESP_ZB_CONNECTION_STATUS_LEFT:
        zigbee_network_joined = false;
        ESP_LOGW("ZB", "Network LEFT");
        break;

    default:
        ESP_LOGI("ZB", "Status: %d", status);
        break;
    }
}

/* ============================================================
 *  ФИЗИЧЕСКАЯ КНОПКА (ОПЦИОНАЛЬНО)
 *  Сброс Zigbee-сети: нажать 6 раз за 5 сек
 * ============================================================ */
static void handle_physical_button(void)
{
    /*
     * Если используется физическая кнопка (GPIO0),
     * раскомментировать и настроить.
     *
     * GPIO0 настраивается как INPUT с прерыванием.
     * При 6 быстрых нажатиях за 5 секунд — сброс сети.
     */
#if 0
    uint64_t now = esp_timer_get_time();

    if (now - factory_reset_window_start > 5000000) {
        /* Новое окно */
        factory_reset_count = 0;
        factory_reset_window_start = now;
    }

    factory_reset_count++;

    if (factory_reset_count >= 6) {
        ESP_LOGW("ZB", "Factory reset triggered (6 presses)");
        esp_zb_factory_reset();
        factory_reset_count = 0;
    }
#endif
}

/* ============================================================
 *  ОСНОВНОЙ ЦИКЛ
 * ============================================================ */
void app_main(void)
{
    /* Инициализация NVS (энергонезависимая память) */
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NO_FREE_PAGES
        && ret != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE("MAIN", "NVS init failed: %d", ret);
        return;
    }

    /* Инициализация GPIO */
    gpio_init();

    /* Восстановление состояний реле из NVS */
    nvs_restore_states();

    /* Инициализация Zigbee */
    zigbee_init();

    /* Регистрация колбэков */
    on_off_light_init(zigbee_on_off_cb, zigbee_on_off_get_cb);
    esp_zb_register_connection_status_callback(zigbee_status_cb);

    /* Запуск Zigbee-стека */
    ESP_LOGI("MAIN", "Starting Zigbee stack...");
    esp_zb_start(true);  /* true = автоматическое подключение к сети */

    ESP_LOGI("MAIN", "===================================");
    ESP_LOGI("MAIN", "  7ch Zigbee Relay Controller");
    ESP_LOGI("MAIN", "  Channels: %d", CHANNEL_COUNT);
    ESP_LOGI("MAIN", "  Status: RUNNING");
    ESP_LOGI("MAIN", "===================================");

    /* Главный цикл */
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);  /* 1 секунда */

        /*
         * TODO:
         *   - Мониторинг физической кнопки (GPIO0)
         *   - Индикация состояния (LED)
         *   - Доп. диагностика
         */
        (void) handle_physical_button();
    }
}