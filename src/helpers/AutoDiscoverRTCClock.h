#pragma once

#include <Mesh.h>
#include <Arduino.h>
#include <Wire.h>

#ifndef RTC_RESYNC_INTERVAL_MS
  #define RTC_RESYNC_INTERVAL_MS  3600000UL   // re-sync system clock from hardware RTC once per hour
#endif

class AutoDiscoverRTCClock : public mesh::RTCClock {
  mesh::RTCClock* _fallback;
  bool _has_hw_rtc;
  unsigned long _last_sync_ms;
  const char* _source_name;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
  void syncSystemClock();
public:
  AutoDiscoverRTCClock(mesh::RTCClock& fallback)
    : _fallback(&fallback), _has_hw_rtc(false), _last_sync_ms(0), _source_name("internal") { }

  const char* getSourceName() const override { return _source_name; }

  void begin(TwoWire& wire);
  uint32_t getCurrentTime() override;
  void setCurrentTime(uint32_t time) override;

  void tick() override {
    _fallback->tick();   // is typically VolatileRTCClock, which now needs tick()
    if (_has_hw_rtc) {
      unsigned long now = millis();
      if ((unsigned long)(now - _last_sync_ms) >= RTC_RESYNC_INTERVAL_MS) {
        syncSystemClock();
        _last_sync_ms = now;
      }
    }
  }
};
