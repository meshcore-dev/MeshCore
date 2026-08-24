#include "ThinkNodeM6Board.h"
#include <Arduino.h>

#ifdef THINKNODE_M6

#include <Wire.h>

void ThinkNodeM6Board::begin() {
  NRF52Board::begin();

  Wire.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

  // Red LED solid while booting; onBootComplete() hands it over to the heartbeat.
  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_RED, LED_STATE_ON);
  _booting = true;

  delay(10); // give sx1262 some time to power up
}

void ThinkNodeM6Board::onBootComplete() {
  // Flash both LEDs together a few times. This is the one moment worth being
  // loud about: it is how you confirm a freshly flashed node actually came up.
  for (uint8_t i = 0; i < BOOT_FLASH_COUNT; i++) {
    digitalWrite(PIN_LED_RED, LED_STATE_ON);
#ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, HIGH);
#endif
    delay(BOOT_FLASH_ON_MS);
    digitalWrite(PIN_LED_RED, !LED_STATE_ON);
#ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, LOW);
#endif
    delay(BOOT_FLASH_OFF_MS);
  }

  _booting = false;
  _status_cycle_start = millis();
  digitalWrite(PIN_LED_RED, !LED_STATE_ON);
}

void ThinkNodeM6Board::updateStatusLed(bool gps_fix) {
  if (_booting) return;  // still solid-on, boot hasn't finished

  unsigned long phase = millis() - _status_cycle_start;  // wrap-safe
  if (phase >= STATUS_LED_PERIOD_MS) {
    _status_cycle_start += STATUS_LED_PERIOD_MS;
    phase -= STATUS_LED_PERIOD_MS;
    // Latch the pattern once per cycle so a fix flapping mid-blink can't
    // produce a half-formed pulse.
    _status_blinks = gps_fix ? 2 : 1;
    if (phase >= STATUS_LED_PERIOD_MS) {  // fell far behind (long sleep); resync
      _status_cycle_start = millis();
      phase = 0;
    }
  }

  bool on = false;
  for (uint8_t i = 0; i < _status_blinks; i++) {
    unsigned long start = i * (unsigned long)(STATUS_LED_ON_MS + STATUS_LED_GAP_MS);
    if (phase >= start && phase < start + STATUS_LED_ON_MS) {
      on = true;
      break;
    }
  }
  digitalWrite(PIN_LED_RED, on ? LED_STATE_ON : !LED_STATE_ON);
}

uint16_t ThinkNodeM6Board::getBattMilliVolts() {
  int adcvalue = 0;

  digitalWrite(PIN_ADC_CTRL, HIGH);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(10);

  // ADC range is 0..3000mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  digitalWrite(PIN_ADC_CTRL, LOW);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * REAL_VBAT_MV_PER_LSB);
}
#endif
