#include <Arduino.h>
#include <Wire.h>

#include "RAKWismeshTagBoard.h"

#ifdef PIN_BUZZER
#include <helpers/ui/buzzer.h>
static genericBuzzer buzzer;
#endif

#ifndef LED_ACTIVE
#ifdef LED_STATE_ON
#define LED_ACTIVE LED_STATE_ON
#else
#define LED_ACTIVE HIGH
#endif
#endif

#ifndef LED_INACTIVE
#define LED_INACTIVE (!LED_ACTIVE)
#endif

void RAKWismeshTagBoard::activityLedOff() {
  if (_activity_led_pin >= 0) {
    digitalWrite(_activity_led_pin, LED_INACTIVE);
    _activity_led_pin = -1;
  }
  _activity_led_until = 0;
}

void RAKWismeshTagBoard::pulseLed(int pin, unsigned long ms) {
  if (pin < 0) return;
  if (_activity_led_pin >= 0 && _activity_led_pin != pin) {
    digitalWrite(_activity_led_pin, LED_INACTIVE);
  }
  digitalWrite(pin, LED_ACTIVE);
  _activity_led_pin = pin;
  _activity_led_until = millis() + ms;
}

void RAKWismeshTagBoard::begin() {
  NRF52BoardDCDC::begin();

  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_USER_BTN, INPUT_PULLUP);

#ifdef LED_GREEN
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, LED_INACTIVE);
#endif
#ifdef LED_BLUE
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LED_INACTIVE);
#endif
#if defined(LED_RED) && (LED_RED >= 0)
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, LED_INACTIVE);
#endif

#ifdef PIN_BUZZER
  buzzer.begin();
#endif

  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up
}

void RAKWismeshTagBoard::onBootComplete() {
#ifdef PIN_BUZZER
  buzzer.quiet(false);
  buzzer.startup();
#endif
}

void RAKWismeshTagBoard::loop() {
#ifdef PIN_BUZZER
  if (buzzer.isPlaying()) {
    buzzer.loop();
  }
#endif

  if (_activity_led_pin >= 0 && _activity_led_until != 0 && millis() >= _activity_led_until) {
    activityLedOff();
  }
}

void RAKWismeshTagBoard::onPacketLed(PacketLedRole role) {
  switch (role) {
    case PACKET_LED_LOCAL:
#ifdef LED_GREEN
      pulseLed(LED_GREEN);
#endif
      break;
    case PACKET_LED_RELAY:
#ifdef LED_BLUE
      pulseLed(LED_BLUE);
#endif
      break;
    case PACKET_LED_UNRELATED:
#if defined(LED_RED) && (LED_RED >= 0)
      pulseLed(LED_RED, 30);
#endif
      break;
  }
}

void RAKWismeshTagBoard::onBeforeTransmit() {
  _activity_led_until = 0;
#ifdef LED_GREEN
  if (_activity_led_pin >= 0 && _activity_led_pin != LED_GREEN) {
    digitalWrite(_activity_led_pin, LED_INACTIVE);
  }
  digitalWrite(LED_GREEN, LED_ACTIVE);
  _activity_led_pin = LED_GREEN;
#endif
}

void RAKWismeshTagBoard::onAfterTransmit() {
#ifdef LED_GREEN
  digitalWrite(LED_GREEN, LED_INACTIVE);
  if (_activity_led_pin == LED_GREEN) {
    _activity_led_pin = -1;
  }
#endif
}
