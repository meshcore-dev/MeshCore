#pragma once

#include <Arduino.h>

// Airoha power sequencing ported from the supplied Meshtastic GPS.cpp.
// Keep the 25-command sleep sequence out of the normal mesh loop's delays.
class MeshTrackerX1GPS {
  enum State { Sleeping, Active, SleepPending };
  Stream& _serial;
  State _state = Sleeping;
  uint8_t _sleep_commands = 0;
  uint32_t _last_command = 0;

  void sendSleepCommand() {
    _serial.write("$PAIR650,0*25\r\n");
    _last_command = millis();
    ++_sleep_commands;
  }

public:
  explicit MeshTrackerX1GPS(Stream& serial) : _serial(serial) {}

  bool isActive() const { return _state == Active; }

  // Returns false for an already running GPS, preserving an existing fix.
  bool start() {
    if (_state == Active) return false;
    // Cancel an unfinished sleep sequence before pulsing the wake input.
    _state = Active;
    _sleep_commands = 0;
    for (int pending = _serial.available(); pending > 0; --pending) {
      _serial.read();
    }

    pinMode(GPS_VRTC_EN, OUTPUT);
    digitalWrite(GPS_VRTC_EN, HIGH);
    pinMode(GPS_RESET, OUTPUT);
    digitalWrite(GPS_RESET, LOW);  // active HIGH reset: preserve backup state
    pinMode(GPS_SLEEP_INT, OUTPUT);
    digitalWrite(GPS_SLEEP_INT, HIGH);
    pinMode(GPS_RTC_INT, OUTPUT);
    digitalWrite(GPS_RTC_INT, LOW);
    pinMode(GPS_EN, OUTPUT);
    digitalWrite(GPS_EN, HIGH);

    delay(50);
    digitalWrite(GPS_RTC_INT, HIGH);
    delay(3);
    digitalWrite(GPS_RTC_INT, LOW);
    delay(50);
    return true;
  }

  void requestSleep() {
    if (_state != Active) return;
    _state = SleepPending;
    _sleep_commands = 0;
    sendSleepCommand();
  }

  void loop() {
    if (_state != SleepPending || uint32_t(millis() - _last_command) < 40) return;
    if (_sleep_commands < 25) {
      sendSleepCommand();
    } else {
      // Also wait 40 ms after the final command before cutting main power.
      digitalWrite(GPS_EN, LOW);
      _state = Sleeping;
      // GPS_VRTC_EN remains HIGH for the next warm start.
    }
  }

  // Only whole-board shutdown may wait for the outstanding command sequence.
  // This must run before the common board helper stops GPS or ends its UART.
  void shutdown() {
    requestSleep();
    while (_state == SleepPending) {
      loop();
      if (_state == SleepPending) delay(1);
    }
  }
};
