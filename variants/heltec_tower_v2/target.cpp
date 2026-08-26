#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

HeltecTowerV2Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
TowerV2ExternalWatchdog external_watchdog;

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}

bool TowerV2ExternalWatchdog::begin() {
  last_feed_watchdog = 0;
  pinMode(EXTERNAL_WATCHDOG_WAKE_PIN, INPUT);
  pinMode(EXTERNAL_WATCHDOG_DONE_PIN, OUTPUT);
  delay(1);
  digitalWrite(EXTERNAL_WATCHDOG_DONE_PIN, LOW);
  delay(1);
  feed();
  return true;
}

void TowerV2ExternalWatchdog::loop() {
  if (millis() - last_feed_watchdog >= EXTERNAL_WATCHDOG_FEED_INTERVAL_MS) {
    feed();
  }
}

unsigned long TowerV2ExternalWatchdog::getIntervalMs() const {
  unsigned long elapsed_ms = millis() - last_feed_watchdog;
  if (elapsed_ms >= EXTERNAL_WATCHDOG_FEED_INTERVAL_MS) {
    return 0;
  }
  return EXTERNAL_WATCHDOG_FEED_INTERVAL_MS - elapsed_ms;
}

void TowerV2ExternalWatchdog::feed() {
  digitalWrite(EXTERNAL_WATCHDOG_DONE_PIN, HIGH);
  delay(1);
  digitalWrite(EXTERNAL_WATCHDOG_DONE_PIN, LOW);
  last_feed_watchdog = millis();
}
