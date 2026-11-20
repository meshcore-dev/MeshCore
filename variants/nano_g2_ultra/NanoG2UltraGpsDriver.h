#pragma once

#include <helpers/sensors/NmeaGpsDriver.h>

class NanoG2UltraGpsDriver : public NmeaGpsDriver {
public:
  NanoG2UltraGpsDriver(UART_TYPE& serial) : NmeaGpsDriver(serial) {
    pinMode(PIN_GPS_STANDBY, OUTPUT);
    digitalWrite(PIN_GPS_STANDBY, LOW);
  }

  void begin() override {
    if (isActive()) return;

    digitalWrite(PIN_GPS_STANDBY, HIGH);
    NmeaGpsDriver::begin();
    delay(1000);
  }

  void stop() override {
    NmeaGpsDriver::stop();
    digitalWrite(PIN_GPS_STANDBY, LOW);
  }

  bool detectAtBoot() {
    begin();
    const bool wasActive = detect(0);
    if (!wasActive) stop();
    return wasActive;
  }
};
