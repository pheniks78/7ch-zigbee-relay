/**
 * Zigbee2MQTT external converter — 7ch-zigbee-relay
 *
 * 7-канальный Zigbee контроллер реле на ESP32-H2 + ULN2003
 * ================================================================
 *
 * Установка:
 *   1. Скопировать файл в папку external_converters Z2M:
 *      cp 7ch_zigbee_relay.js /config/zigbee2mqtt/data/external_converters/
 *   2. В configuration.yaml Z2M добавить:
 *      external_converters:
 *        - 7ch_zigbee_relay.js
 *   3. Перезапустить Zigbee2MQTT
 *   4. Заново сопрячь устройство (при необходимости — permit join)
 *
 * После сопряжения в Home Assistant появится одно устройство
 * с 7 независимыми переключателями.
 *
 * Совместимость: Z2M 2.x, zigbee-herdsman-converters v20+
 * ================================================================
 */

const {definitions} = require('zigbee-herdsman-converters/converters/constants');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const e = require('zigbee-herdsman-converters/lib/exposes').presets;
const reporting = require('zigbee-herdsman-converters/lib/reporting');

/* ================================================================
 *  КОНФИГУРАЦИЯ КАНАЛОВ
 * ================================================================ */
const CHANNEL_COUNT = 7;

/* Имена endpoint'ов в Z2M (l1..l7) и их свойства (state_l1..state_l7) */
const endpoints = [];
for (let i = 1; i <= CHANNEL_COUNT; i++) {
    endpoints.push({
        endpoint: `l${i}`,       /* метка endpoint'а */
        property: `state_l${i}`, /* свойство в MQTT-топике */
        name: `Канал ${i}`,
    });
}

/* ================================================================
 *  ОПРЕДЕЛЕНИЕ УСТРОЙСТВА
 * ================================================================ */
const definition = {
    /* ----- ИДЕНТИФИКАЦИЯ (как Z2M находит это устройство) ----- */
    zigbeeModel: ['7ch-zigbee-relay'],
    model: '7ch-zigbee-relay',
    vendor: 'ESP32-H2',
    description: '7-канальный Zigbee контроллер реле',

    /* Второй вариант идентификации — по производителю (дефолт Espressif) */
    fingerprint: [
        {manufacturerName: 'ESPRESSIF'},
    ],

    /* ----- ПРЕОБРАЗОВАТЕЛИ ----- */
    fromZigbee: [fz.onOff],
    toZigbee: [tz.onOff],

    /* ----- СУЩНОСТИ (7 переключателей) ----- */
    exposes: endpoints.map((ep) =>
        e.switch()
            .withEndpoint(ep.endpoint)
            .withProperty(ep.property)
            .withDescription(ep.name)
    ),

    /* ----- МУЛЬТИ-ENDPOINT РЕЖИМ ----- */
    meta: {
        multiEndpoint: true,
        multiEndpointSkip: [],
    },

    /* ----- НАСТРОЙКА ПРИ ПОДКЛЮЧЕНИИ ----- */
    configure: async (device, coordinatorEndpoint) => {
        /* Привязываем On/Off кластер на каждом endpoint'е к координатору */
        for (let ep = 1; ep <= CHANNEL_COUNT; ep++) {
            const epId = ep.toString();
            await reporting.bind(device, coordinatorEndpoint, epId, [
                'genOnOff',
            ]);

            /* Настраиваем репортинг: отправлять состояние при изменении */
            await reporting.onOff(device, epId, {
                minInterval: 0,      /* сразу при изменении */
                maxInterval: 3600,   /* не чаще раза в час */
                minChange: 1,        /* менять при любом изменении */
            });
        }
    },
};

/* Регистрируем конвертер */
definitions.push(definition);

/* ================================================================
 *  ЕСЛИ УСТРОЙСТВО НЕ ОПОЗНАЁТСЯ АВТОМАТИЧЕСКИ
 * ================================================================
 *
 * Убедись, что в configuration.yaml Z2M есть:
 *
 *   external_converters:
 *     - 7ch_zigbee_relay.js
 *
 * Если устройство уже было сопряжено до добавления конвертера:
 *   1. Удали устройство из Z2M (Settings → Devices → Remove)
 *   2. Включи Permit Join
 *   3. Перезагрузи ESP32-H2 (сброс питания)
 *   4. Подожди, пока устройство переподключится
 *
 * ================================================================
 *  КОНТРОЛЬ СОСТОЯНИЯ РЕЛЕ ПОСЛЕ ПЕРЕЗАГРУЗКИ
 * ================================================================
 *
 * После перезагрузки ESP32-H2 восстанавливает последние состояния
 * из NVS, но не шлёт Zigbee-репорт автоматически.
 *
 * Чтобы синхронизировать HA с реальным состоянием, в Z2M можно
 * включить опрос:
 *
 *   devices:
 *     '0xXXXX':
 *       debounce: 1
 *       homeassistant:
 *         polling_intensity: 60
 *
 * Либо настроить в HA automation на перезагрузку Zigbee2MQTT:
 *   trigger: state entity_id: binary_sensor.esp32_h2_online
 *   from: 'off' to: 'on'
 *   action: delay 00:00:05 → mqtt.publish topic zigbee2mqtt/0xXXXX/get
 *            payload: {"state_l1": "", "state_l2": "", ...}
 *
 * ================================================================ */

console.log('7ch-zigbee-relay converter loaded ✓');