#pragma once

#include <Mesh.h>
#include <Arduino.h>
#include <Wire.h>

class AutoDiscoverRTCClock : public mesh::RTCClock {
  mesh::RTCClock* _fallback;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
public:
  AutoDiscoverRTCClock(mesh::RTCClock& fallback) : _fallback(&fallback) { }

  // begin() stores an RV3028's backup switchover config in its EEPROM, and getCurrentTime()
  // retries that a few times if it failed, so either can block briefly on the RTC
  void begin(TwoWire& wire);
  uint32_t getCurrentTime() override;
  void setCurrentTime(uint32_t time) override;

  void tick() override {
    _fallback->tick();   // is typically VolatileRTCClock, which now needs tick()
  }
};
