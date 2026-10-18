#include <Arduino.h>
#include "ThinkNodeM4Board.h"
#include <Wire.h>

#include <bluefruit.h>

#define BATT_GAUGE_TIMEOUT_MS  10000

void ThinkNodeM4Board::begin() {
  NRF52Board::begin();
  btn_prev_state = HIGH;

  pinMode(LED_BAT1, OUTPUT);
  pinMode(LED_BAT2, OUTPUT);
  pinMode(LED_BAT3, OUTPUT);
  pinMode(LED_BAT4, OUTPUT);
  clearBatteryGauge();

  // power-on indication: 3 quick red flashes
  pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LED_STATE_ON);
    delay(100);
    digitalWrite(LED_PIN, !LED_STATE_ON);
    delay(100);
  }

  Wire.begin();
  battery_serial->begin(4800);

  delay(10);   // give sx1262 some time to power up
}

void ThinkNodeM4Board::clearBatteryGauge() {
  digitalWrite(LED_BAT1, LOW);
  digitalWrite(LED_BAT2, LOW);
  digitalWrite(LED_BAT3, LOW);
  digitalWrite(LED_BAT4, LOW);
}

void ThinkNodeM4Board::showBatteryGauge() {
  getBattMilliVolts();   // refresh bat_level_percent if a frame from the battery MCU is waiting
  uint8_t pct = bat_level_percent;
  digitalWrite(LED_BAT1, pct >= 1  ? HIGH : LOW);
  digitalWrite(LED_BAT2, pct >= 25 ? HIGH : LOW);
  digitalWrite(LED_BAT3, pct >= 50 ? HIGH : LOW);
  digitalWrite(LED_BAT4, pct >= 75 ? HIGH : LOW);
  gauge_on_since = millis();
}

void ThinkNodeM4Board::loop() {
  NRF52BoardDCDC::loop();

#ifdef BUTTON_PIN
  uint8_t v = digitalRead(BUTTON_PIN);
  if (v != gauge_btn_prev) {
    gauge_btn_prev = v;
    if (v == USER_BTN_PRESSED) showBatteryGauge();
  }
#endif

  if (gauge_on_since && millis() - gauge_on_since >= BATT_GAUGE_TIMEOUT_MS) {
    clearBatteryGauge();
    gauge_on_since = 0;
  }
}

uint16_t ThinkNodeM4Board::getBattMilliVolts() {
  int tentatives = 10;
  uint8_t data[5];
  uint8_t b;

  if (battery_serial->available() < 10)
    return bat_level_mv;

  // discard old data
  while (battery_serial->available() > 10)
    battery_serial->read();

  // synchronize
  while((b = battery_serial->read()) != 0xFE) {
    if (tentatives-- == 0) {
      MESH_DEBUG_PRINTLN("Could not find battery frame start %x", b);
      return bat_level_mv;
    }
  }
  battery_serial->readBytes(data, 5);

  if (data[4] != 0xFD) {
    MESH_DEBUG_PRINTLN("Invalid battery frame end %x", data[4]);
    return bat_level_mv;
  }
  bat_level_percent = data[0];
  //MESH_DEBUG_PRINTLN("Battery level %d\%", bat_level_percent);
  bat_level_mv = 2 * (data[1]*1000. + data[2]*10. + data[3]/10.);
  return bat_level_mv;
}
