# 7ch-zigbee-relay

**7-канальный Zigbee-контроллер реле на ESP32-H2 + ULN2003 для Home Assistant**

Полностью открытая альтернатива платной прошивке с Lava.top. MIT-лицензия.

---

## Возможности

| Функция | Платный (Lava.top) | Этот проект |
|---|---|---|
| Кол-во каналов | 7 | 1–7 (настраивается) |
| Нативный Zigbee | ✅ | ✅ (Zigbee Router) |
| Home Assistant / ZHA | ✅ | ✅ |
| Отдельное управление каждым каналом | ✅ | ✅ |
| Обратная связь | ✅ | ✅ |
| Локальное управление без Zigbee | ✅ | ✅ |
| Сохранение состояний (NVS) | ✅ | ✅ |
| Защита от дребезга | ✅ | ✅ |
| Сброс сети без разборки | ✅ | ✅ (6 нажатий за 5 сек) |
| Физические выключатели | ❌ опционально | ✅ |
| OTA-обновления | ❌ | ✅ |
| **Цена** | **~$10** | **$0** |
| **Исходный код** | ❌ закрыт | ✅ открыт (MIT) |

## Аппаратная часть

```
ESP32-H2 Super Mini
         │
         ├─ GPIO2 ──→ ULN2003 IN1 ──→ Реле 1
         ├─ GPIO3 ──→ ULN2003 IN2 ──→ Реле 2
         ├─ GPIO4 ──→ ULN2003 IN3 ──→ Реле 3
         ├─ GPIO5 ──→ ULN2003 IN4 ──→ Реле 4
         ├─ GPIO6 ──→ ULN2003 IN5 ──→ Реле 5
         ├─ GPIO7 ──→ ULN2003 IN6 ──→ Реле 6
         └─ GPIO8 ──→ ULN2003 IN7 ──→ Реле 7
```

**Что нужно купить:**
- ESP32-H2 Super Mini (~$3–4, AliExpress)
- ULN2003 (~$1, китайский 7-канальный драйвер реле)
- 7 реле (SRD, любые 5V/12V на 10A)
- Блок питания 5V (USB или внешний)
- Провода, корпус

## Сборка прошивки

### 1. Установка ESP-IDF

```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32h2
source export.sh
```

### 2. Склонировать ESP-Zigbee-SDK

```bash
git clone https://github.com/espressif/esp-zigbee-sdk.git
```

### 3. Собрать проект

```bash
cd 7ch-zigbee-relay
idf.py set-target esp32h2
idf.py build
```

### 4. Загрузить на ESP32-H2

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

(на Linux порт обычно `/dev/ttyUSB0` или `/dev/ttyACM0`)

## Подключение к Home Assistant

1. Открой ZHA (Zigbee Home Automation) в HA → «Добавить устройство»
2. Подай питание на ESP32-H2
3. Устройство появится как 7 независимых `On/Off Light` (или `Switch`)
4. Переименуй в HA под свои нужды

Либо через **Zigbee2MQTT**: создай external converter (см. `/zigbee2mqtt/` в этом репо).

## OTA-обновление

После первой загрузки по USB, прошивку можно обновлять по Zigbee:

```bash
python make_ota.py build/7ch-zigbee-relay.bin
```

Скопируй `.ota` файл в OTA-папку HA (обычно `config/zha_ota/`).

## Структура проекта

```
7ch-zigbee-relay/
├── CMakeLists.txt       # Проект ESP-IDF
├── sdkconfig            # Конфигурация SDK
├── make_ota.py          # Генератор OTA-образа
├── main/
│   ├── CMakeLists.txt   # Компонент ESP-IDF
│   ├── main.c           # Основная прошивка
│   └── relay_control.h  # Управление реле
└── README.md
```

## Лицензия

MIT. Делайте что хотите.