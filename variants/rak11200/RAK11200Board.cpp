#include "RAK11200Board.h"

void RAK11200Board::begin() {
  ESP32Board::begin();

  pinMode(PIN_VBAT_READ, INPUT);

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif
}

uint16_t RAK11200Board::getBattMilliVolts() {
  analogReadResolution(12);

  uint32_t raw = 0;
  const int BATTERY_SAMPLES = 8;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    raw += analogRead(PIN_VBAT_READ);
  }
  raw = raw / BATTERY_SAMPLES;

  return (ADC_MULTIPLIER * raw) / 4096;
}

const char* RAK11200Board::getManufacturerName() const {
  return "RAK 11200";
}
