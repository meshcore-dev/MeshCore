#include "m1_leds.h"

#ifdef THINKNODE_M1

// 时间参数（评审要求）
#define M1_PAIR_BLINK_MILLIS      500    // 1Hz 闪烁（500ms 亮 / 500ms 灭）
#define M1_PAIR_WINDOW_MILLIS     30000  // 配对窗口最长 30s
#define M1_PAIRED_SOLID_MILLIS    30000  // 配对成功常亮 30s
#define M1_RX_PULSE_MILLIS        60     // 收到数据时的一次闪烁脉宽
#define M1_RX_MIN_GAP_MILLIS      150    // 连收时避免常亮
#define M1_LOWBATT_BLINK_MILLIS   500    // 低电量 1Hz 闪烁

enum M1LedMode : uint8_t {
  M1_LED_IDLE = 0,
  M1_LED_PAIRING,
  M1_LED_PAIRED,
  M1_LED_LOW_BATT
};

static M1LedMode s_mode = M1_LED_IDLE;
static unsigned long s_mode_since = 0;
static unsigned long s_next_toggle = 0;
static unsigned long s_rx_until = 0;
static unsigned long s_rx_hold = 0;
static bool s_blink_on = false;
static volatile bool s_low_batt = false;
#ifdef M1_LOW_BATT_ON_RED
static SoftwareTimer s_low_batt_timer;
static bool s_low_batt_phase = false;

static void low_batt_timer_callback(TimerHandle_t) {
  // RTOS timer task continues while the UI waits for the e-paper BUSY pin.
  taskENTER_CRITICAL();
  if (s_low_batt) {
    s_low_batt_phase = !s_low_batt_phase;
    digitalWrite(M1_LOW_BATT_LED_PIN, s_low_batt_phase ? M1_LOW_BATT_LED_ON
                 : (M1_LOW_BATT_LED_ON == HIGH ? LOW : HIGH));
  }
  taskEXIT_CRITICAL();
}
#endif

static inline void led_drive(int pin, bool on, int on_level) {
  digitalWrite(pin, on ? on_level : (on_level == HIGH ? LOW : HIGH));
}

static inline void status_off() {
#ifdef M1_STATUS_LED_PIN
  led_drive(M1_STATUS_LED_PIN, false, M1_STATUS_LED_ON);
#endif
#ifdef M1_LOW_BATT_ON_RED
  led_drive(M1_LOW_BATT_LED_PIN, false, M1_LOW_BATT_LED_ON);
#endif
}

void m1_leds_begin() {
#ifdef M1_STATUS_LED_PIN
  pinMode(M1_STATUS_LED_PIN, OUTPUT);
#endif
#ifdef M1_LOW_BATT_ON_RED
  pinMode(M1_LOW_BATT_LED_PIN, OUTPUT);
#endif
  s_mode = M1_LED_IDLE;
  s_blink_on = false;
  s_low_batt = false;
  status_off();
#ifdef M1_LOW_BATT_ON_RED
  s_low_batt_timer.begin(M1_LOWBATT_BLINK_MILLIS, low_batt_timer_callback);
#endif
}

void m1_leds_ble_pairing_started() {
  s_mode = M1_LED_PAIRING;
  s_mode_since = millis();
  s_blink_on = true;
  s_next_toggle = s_mode_since + M1_PAIR_BLINK_MILLIS;
}

void m1_leds_ble_pairing_secured() {
  s_mode = M1_LED_PAIRED;
  s_mode_since = millis();
  s_blink_on = true;   // 常亮由 loop 统一点亮
}

void m1_leds_ble_pairing_cancelled() {
  if (s_mode == M1_LED_PAIRING || s_mode == M1_LED_PAIRED) {
    s_mode = s_low_batt ? M1_LED_LOW_BATT : M1_LED_IDLE;
    s_mode_since = millis();
    s_blink_on = false;
  }
}

void m1_leds_rx_activity() {
  unsigned long now = millis();
  if (s_mode == M1_LED_PAIRING || s_mode == M1_LED_PAIRED) return;  // 配对指示优先
  if (now < s_rx_hold) return;                                      // 限速
  s_rx_until = now + M1_RX_PULSE_MILLIS;
  s_rx_hold = now + M1_RX_PULSE_MILLIS + M1_RX_MIN_GAP_MILLIS;
}

void m1_leds_low_battery(bool active) {
  if (active == s_low_batt) return;
#ifdef M1_LOW_BATT_ON_RED
  taskENTER_CRITICAL();
  s_low_batt = active;
  s_low_batt_phase = active;
  led_drive(M1_LOW_BATT_LED_PIN, active, M1_LOW_BATT_LED_ON);
  taskEXIT_CRITICAL();
  if (s_low_batt_timer.getHandle()) {
    if (active) xTimerReset(s_low_batt_timer.getHandle(), portMAX_DELAY);
    else xTimerStop(s_low_batt_timer.getHandle(), portMAX_DELAY);
  }
#else
  s_low_batt = active;
  if (s_mode == M1_LED_PAIRING || s_mode == M1_LED_PAIRED) return;  // 配对结束后再进入
  if (active) {
    s_mode = M1_LED_LOW_BATT;
    s_mode_since = millis();
    s_blink_on = true;
    s_next_toggle = s_mode_since + M1_LOWBATT_BLINK_MILLIS;
  } else {
    s_mode = M1_LED_IDLE;
    s_mode_since = millis();
    s_blink_on = false;
  }
#endif
}

void m1_leds_shutdown() {
  m1_leds_low_battery(false); // Stop callback writes before final sleep GPIO setup.
  status_off();
}

bool m1_leds_is_low_battery() {
  return s_low_batt;
}

void m1_leds_loop() {
  unsigned long now = millis();
  bool status_on = false;

  switch (s_mode) {
    case M1_LED_PAIRING:
      if (now - s_mode_since >= M1_PAIR_WINDOW_MILLIS) {   // 30s 未完成配对，停止闪烁
        s_mode = s_low_batt ? M1_LED_LOW_BATT : M1_LED_IDLE;
        s_mode_since = now;
        s_blink_on = false;
        if (s_mode == M1_LED_LOW_BATT) s_next_toggle = now + M1_LOWBATT_BLINK_MILLIS;
      } else if (now >= s_next_toggle) {
        s_blink_on = !s_blink_on;
        s_next_toggle = now + M1_PAIR_BLINK_MILLIS;
      }
      status_on = s_blink_on;
      break;

    case M1_LED_PAIRED:
      if (now - s_mode_since >= M1_PAIRED_SOLID_MILLIS) {  // 常亮 30s 后熄灭
        s_mode = s_low_batt ? M1_LED_LOW_BATT : M1_LED_IDLE;
        s_mode_since = now;
        s_blink_on = false;
        if (s_mode == M1_LED_LOW_BATT) s_next_toggle = now + M1_LOWBATT_BLINK_MILLIS;
      } else {
        status_on = true;
      }
      break;

    case M1_LED_LOW_BATT:
      if (now >= s_next_toggle) {
        s_blink_on = !s_blink_on;
        s_next_toggle = now + M1_LOWBATT_BLINK_MILLIS;
      }
#ifdef M1_LOW_BATT_ON_RED
      if (now < s_rx_until) status_on = true; // Red LED has an independent timer.
#else
      status_on = s_blink_on;
#endif
      break;

    case M1_LED_IDLE:
    default:
      if (now < s_rx_until) status_on = true;   // 收到数据的提示脉冲
      break;
  }

#ifdef M1_STATUS_LED_PIN
  led_drive(M1_STATUS_LED_PIN, status_on, M1_STATUS_LED_ON);
#endif
}

#endif  // THINKNODE_M1
