#include <Arduino.h>
#include <Wire.h>
#include <HardwarePWM.h>

#include "SenseCapSolarBoard.h"

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void SenseCapSolarBoard::initiateShutdown(uint8_t reason) {
  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);

  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, enable_lpcomp ? LOW : HIGH);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void SenseCapSolarBoard::begin() {
  NRF52BoardDCDC::begin();

  pinMode(BATTERY_PIN, INPUT);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
  analogReadResolution(12);
  analogReference(AR_INTERNAL_3_0);
  delay(50);

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#elif defined(PIN_BUTTON1)
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
#endif

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

#ifdef LED_WHITE
  pinMode(LED_WHITE, OUTPUT);
  digitalWrite(LED_WHITE, LOW);
#ifdef SENSECAP_SOLAR_WHITE_LED
  white_led_last_pulse_at_ = millis();   // first heartbeat pulse 5 s after boot
#endif
#endif
#ifdef LED_BLUE
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);
#endif

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif

  delay(10);   // give sx1262 some time to power up
}

#ifdef SENSECAP_SOLAR_WHITE_LED
// Dimmed WHITE heartbeat using hardware PWM (repeater builds only).
//
// Peripheral audit for this firmware: no analogWrite()/tone() callers, no
// NeoPixel/Servo, Bluefruit52Lib uses no PWM, SoftDevice S140 does not own
// any PWM instance -> NRF_PWM0 (HwPWM0) is free. takeOwnership() fences the
// instance so a future analogWrite() caller cannot claim it.
//
// Timing: base clock 16 MHz (DIV_1), COUNTERTOP 16000 -> exactly 1 kHz.
// Duty: HardwarePWM non-inverted values place bit15 (active-HIGH polarity),
// so compare 1600 => HIGH for [0,1600)/16000 => 10% duty, active-high.
static const uint32_t WHITE_LED_PWM_TOKEN = 0x5031574C;  // 'P1WL'
static const uint16_t WHITE_LED_PWM_TOP   = 16000;       // 16 MHz / 16000 = 1 kHz
static const uint16_t WHITE_LED_PWM_DUTY  = 1600;        // 1600 / 16000 = 10 %
static_assert(WHITE_LED_PWM_TOP <= 0x7FFF, "PWM compare values are 15-bit");
static_assert(WHITE_LED_PWM_DUTY < WHITE_LED_PWM_TOP, "duty must stay below COUNTERTOP");

void SenseCapSolarBoard::whiteLedHeartbeatOn() {
  HwPWM0.takeOwnership(WHITE_LED_PWM_TOKEN);  // no-op once owned; keeps analogWrite away
  HwPWM0.setClockDiv(PWM_PRESCALER_PRESCALER_DIV_1);  // 16 MHz base clock
  HwPWM0.setMaxValue(WHITE_LED_PWM_TOP);              // 1 kHz period
  HwPWM0.addPin(LED_WHITE);                           // drives pin LOW, connects PSEL
  HwPWM0.writePin(LED_WHITE, WHITE_LED_PWM_DUTY);     // enables + starts 10% PWM
}

void SenseCapSolarBoard::whiteLedHeartbeatOff() {
  HwPWM0.stop();                              // ENABLE=0: waveform and its HFCLK demand stop
  if (HwPWM0.checkPin(LED_WHITE)) {
    HwPWM0.removePin(LED_WHITE);              // disconnect PSEL; GPIO block drives again
  }
  pinMode(LED_WHITE, OUTPUT);
  digitalWrite(LED_WHITE, LOW);               // deterministic LOW during the 4900 ms off phase
}

// 100 ms pulse every 5000 ms, non-blocking millis() with wrap-safe unsigned
// arithmetic. Called from loop(); frozen while the OTA steady-on owns the LED.
void SenseCapSolarBoard::updateWhiteStatusLed() {
  if (white_led_ota_active_) return;

  const uint32_t now = millis();
  const uint32_t elapsed = (uint32_t)(now - white_led_last_pulse_at_);
  if (white_led_pulse_active_ && elapsed >= 100U) {
    whiteLedHeartbeatOff();   // stop PWM, pin LOW
    white_led_pulse_active_ = false;
  }
  if (!white_led_pulse_active_ && elapsed >= 5000U) {
    whiteLedHeartbeatOn();    // 1 kHz hardware PWM, 10% duty
    white_led_pulse_active_ = true;
    white_led_last_pulse_at_ = now;
  }
}

void SenseCapSolarBoard::loop() {
  mesh::MainBoard::loop();   // keep base housekeeping (watchdog etc.) if it ever gains any
  updateWhiteStatusLed();
}

// `start ota` keeps WHITE on for as long as the application is advertising
// its BLE OTA service. The heartbeat PWM is stopped and the pin released
// first, so steady-on is a plain full-brightness GPIO drive (no PWM vs
// digitalWrite conflict). On a failed start nothing changes: the heartbeat
// keeps its normal blinking.
bool SenseCapSolarBoard::startOTAUpdate(const char *id, char reply[]) {
  const bool started = NRF52Board::startOTAUpdate(id, reply);
  if (started) {
    white_led_ota_active_ = true;
    whiteLedHeartbeatOff();
    digitalWrite(LED_WHITE, LED_STATE_ON);
  }
  return started;
}
#endif  // SENSECAP_SOLAR_WHITE_LED
