#pragma once

#include <Arduino.h>
#include "LPPDataHelpers.h"

// Stub sensor implememtation.
class WindSensor {
  uint16_t _rain_tip_count = 0;
  uint32_t _last_tip_ms = 0;

public:
  void query(uint8_t channel, LPPWriter& writer) {
    writer.writeDirection(channel, readDirection());
    writer.writeWindSpeed(channel, readSpeed());
    writer.writeWindGust(channel, readGust());
    writer.writeRain(channel, readRainTipCount());
  }

  uint16_t readSpeed() const {
    // synthetic 0.5-1.5 m/s-ish triangle, just to give TimeSeriesData variation
    uint32_t t = millis() / 1000;
    return 50 + (t % 100);  
  }

  uint16_t readGust() const { return readSpeed() + 30; }

  // TODO: replace with real wind-vane ADC reading once hardware is bench-tested.
  uint16_t readDirection() const { return 180; }

  // TODO: replace with real reed-switch interrupt count once hardware is bench-tested.
  // Ticks once every 10s so the raw counter visibly advances without any real gauge.
  uint16_t readRainTipCount() {
    uint32_t now = millis();
    if (now - _last_tip_ms >= 10000) {
      _rain_tip_count++;
      _last_tip_ms = now;
    }
    return _rain_tip_count;
  }
};
