#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdbool.h>

/* ==== НАСТРОЙКИ КОЛИЧЕСТВА КАНАЛОВ ==== */
#define RELAY_CHANNELS  7   /* Можно менять: 1..7 */

/* ==== РАСПИНОВКА GPIO (ULN2003 входы) ====
 * ESP32-H2 Super Mini → ULN2003
 * Каждый GPIO управляет одним каналом ULN2003
 * По умолчанию: IN1..IN7 ULN2003 → GPIO2,3,4,5,6,7,8
 * Реле подключаются к выходам ULN2003 (OUT1..OUT7)
 * Питание ULN2003: 5V (GND+VCC) от ESP или внешнего БП
 */
#define RELAY_GPIO_INIT { 2, 3, 4, 5, 6, 7, 8 }

/* ==== ЗАЩИТА ОТ ДРЕБЕЗГА ==== */
#define DEBOUNCE_MS     50    /* мс: мин. пауза между переключениями */

/* ==== НАЗВАНИЯ КАНАЛОВ (видны в HA) ==== */
#define CHANNEL_NAMES "Канал 1", "Канал 2", "Канал 3", \
                      "Канал 4", "Канал 5", "Канал 6", "Канал 7"

/* ======================================== */

/* Инициализация GPIO всех каналов */
void relay_gpio_init(void);

/* Установить состояние канала (0=OFF, 1=ON) */
void relay_set_state(int channel, bool state);

/* Получить текущее состояние канала */
bool relay_get_state(int channel);

/* Переключить канал */
void relay_toggle(int channel);

/* Сохранить состояния всех каналов в NVS */
void relay_save_states(void);

/* Восстановить состояния всех каналов из NVS */
void relay_restore_states(void);

#endif /* RELAY_CONTROL_H */