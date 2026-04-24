#pragma once

#include <Arduino.h>

#define LED_STATUS_OFF        0
#define LED_STATUS_BOOT_30S   1
#define LED_STATUS_SLOW_BLINK 2
#define LED_STATUS_ALWAYS_ON  3

#define LED_ACTIVITY_OFF      0
#define LED_ACTIVITY_BLE      1
#define LED_ACTIVITY_LORA     2
#define LED_ACTIVITY_BOTH     3

#define LED_CONN_IDLE         0
#define LED_CONN_ADVERTISING  1
#define LED_CONN_CONNECTED    2

#define LED_BOOT_TIMEOUT_MS   30000
#define LED_BLINK_CYCLE_MS    4000
#define LED_BLINK_ON_MS       200
#define LED_ADV_BLINK_MS      500

class LEDManager {
public:
  LEDManager(int8_t statusPin, int8_t activityPin, bool statusActiveHigh = true, bool activityActiveHigh = true)
    : _statusPin(statusPin), _activityPin(activityPin),
      _statusActiveHigh(statusActiveHigh), _activityActiveHigh(activityActiveHigh) {}

  void begin(uint8_t statusMode, uint8_t activityMode) {
    _statusMode = statusMode;
    _activityMode = activityMode;
    _bootTime = millis();

    if (_statusPin >= 0) {
      pinMode(_statusPin, OUTPUT);
      applyStatusLED();
    }
    if (_activityPin >= 0) {
      pinMode(_activityPin, OUTPUT);
      activityWrite(false);
    }
  }

  void loop() {
    unsigned long now = millis();

    // Status LED logic
    if (_statusPin >= 0) {
      if (_statusMode == LED_STATUS_BOOT_30S && _statusOn && (now - _bootTime >= LED_BOOT_TIMEOUT_MS)) {
        statusWrite(false);
        _statusOn = false;
      } else if (_statusMode == LED_STATUS_SLOW_BLINK) {
        if (now - _lastBlinkToggle >= (_statusOn ? LED_BLINK_ON_MS : LED_BLINK_CYCLE_MS)) {
          _statusOn = !_statusOn;
          statusWrite(_statusOn);
          _lastBlinkToggle = now;
        }
      }
    }

    // Activity LED: BLE advertising blink
    if (_activityPin >= 0 && activityWantsBLE() && _activityConnState == LED_CONN_ADVERTISING) {
      if (now - _lastAdvToggle >= LED_ADV_BLINK_MS) {
        _advBlinkOn = !_advBlinkOn;
        activityWrite(_advBlinkOn);
        _lastAdvToggle = now;
      }
    }
  }

  void onBeforeTransmit() {
    if (_activityPin >= 0 && activityWantsLoRa()) {
      activityWrite(true);
    }
  }

  void onAfterTransmit() {
    if (_activityPin >= 0 && activityWantsLoRa()) {
      // If BLE is connected, keep it on; if advertising, resume blink; otherwise off
      if (activityWantsBLE()) {
        if (_activityConnState == LED_CONN_CONNECTED) {
          activityWrite(true);
        } else if (_activityConnState == LED_CONN_ADVERTISING) {
          // loop() will handle blinking
        } else {
          activityWrite(false);
        }
      } else {
        activityWrite(false);
      }
    }
  }

  void setActivityState(uint8_t state) {
    _activityConnState = state;
    if (_activityPin < 0 || !activityWantsBLE()) return;

    if (state == LED_CONN_CONNECTED) {
      _advBlinkOn = false;
      activityWrite(true);
    } else if (state == LED_CONN_ADVERTISING) {
      _lastAdvToggle = millis();
      _advBlinkOn = true;
      activityWrite(true);
    } else {
      _advBlinkOn = false;
      activityWrite(false);
    }
  }

  void setStatusMode(uint8_t mode) {
    _statusMode = mode;
    _bootTime = millis();
    _lastBlinkToggle = millis();
    _statusOn = false;
    if (_statusPin >= 0) applyStatusLED();
  }

  void setActivityMode(uint8_t mode) {
    _activityMode = mode;
    if (_activityPin >= 0) {
      activityWrite(false);
      _advBlinkOn = false;
      // Re-apply current connection state with new mode
      if (activityWantsBLE()) setActivityState(_activityConnState);
    }
  }

  uint8_t getStatusMode() const { return _statusMode; }
  uint8_t getActivityMode() const { return _activityMode; }

  void powerOff() {
    if (_statusPin >= 0) statusWrite(false);
    if (_activityPin >= 0) activityWrite(false);
  }

private:
  int8_t _statusPin;
  int8_t _activityPin;
  bool _statusActiveHigh;
  bool _activityActiveHigh;
  uint8_t _statusMode = LED_STATUS_OFF;
  uint8_t _activityMode = LED_ACTIVITY_OFF;
  uint8_t _activityConnState = LED_CONN_IDLE;
  unsigned long _bootTime = 0;
  unsigned long _lastBlinkToggle = 0;
  unsigned long _lastAdvToggle = 0;
  bool _statusOn = false;
  bool _advBlinkOn = false;

  bool activityWantsBLE() const {
    return _activityMode == LED_ACTIVITY_BLE || _activityMode == LED_ACTIVITY_BOTH;
  }

  bool activityWantsLoRa() const {
    return _activityMode == LED_ACTIVITY_LORA || _activityMode == LED_ACTIVITY_BOTH;
  }

  void statusWrite(bool on) { digitalWrite(_statusPin, on == _statusActiveHigh ? HIGH : LOW); }
  void activityWrite(bool on) { digitalWrite(_activityPin, on == _activityActiveHigh ? HIGH : LOW); }

  void applyStatusLED() {
    switch (_statusMode) {
      case LED_STATUS_BOOT_30S:
      case LED_STATUS_ALWAYS_ON:
        statusWrite(true);
        _statusOn = true;
        break;
      case LED_STATUS_SLOW_BLINK:
        _lastBlinkToggle = millis();
        _statusOn = false;
        statusWrite(false);
        break;
      default:
        statusWrite(false);
        _statusOn = false;
        break;
    }
  }
};
