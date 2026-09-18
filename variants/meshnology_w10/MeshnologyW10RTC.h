#pragma once

#include <MeshCore.h>
#include <Wire.h>
#include <RTClib.h>

// PCF85063A at I2C address 0x51, seeding and persisting the wrapped system clock.
// AutoDiscoverRTCClock probes 0x51 as a PCF8563 (RTClib RTC_PCF8563), whose register map
// differs (seconds at 0x02 versus 0x04 here), so this board selects the chip explicitly.
class MeshnologyW10RTC : public mesh::RTCClock {
  static constexpr uint8_t ADDRESS = 0x51;

  mesh::RTCClock& _clock;
  TwoWire& _wire;
  bool _present = false;

  bool read(uint8_t reg, uint8_t* data, uint8_t len) {
    _wire.beginTransmission(ADDRESS);
    _wire.write(reg);
    if (_wire.endTransmission() != 0 || _wire.requestFrom(ADDRESS, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) {
      int value = _wire.read();
      if (value < 0) return false;
      data[i] = value;
    }
    return true;
  }

  bool write(uint8_t reg, const uint8_t* data, uint8_t len) {
    _wire.beginTransmission(ADDRESS);
    _wire.write(reg);
    _wire.write(data, len);
    return _wire.endTransmission() == 0;
  }

  static uint8_t bcd(uint8_t value) { return (value / 10) * 16 + value % 10; }
  static uint8_t decimal(uint8_t value) { return (value >> 4) * 10 + (value & 15); }

public:
  MeshnologyW10RTC(mesh::RTCClock& clock, TwoWire& wire) : _clock(clock), _wire(wire) { }

  bool begin() {
    uint8_t control, data[7];
    _present = read(0, &control, 1) && read(4, data, sizeof(data));
    // Reject state left by another application
    if (!_present || (control & 0x22) || (data[0] & 0x80)) return false;
    const uint8_t masks[] = {0x7F, 0x7F, 0x3F, 0x3F, 0x07, 0x1F, 0xFF};
    for (uint8_t i = 0; i < sizeof(data); i++) {
      data[i] &= masks[i];
      if ((data[i] & 15) > 9 || (data[i] >> 4) > 9) return false;
      data[i] = decimal(data[i]);
    }
    // Pre-validate field ranges. RTCLib unsafely indexes a month table
    if (data[0] > 59 || data[1] > 59 || data[2] > 23 || data[3] < 1 || data[3] > 31
        || data[4] > 6 || data[5] < 1 || data[5] > 12) return false;
    DateTime dt(2000 + data[6], data[5], data[3], data[2], data[1], data[0]);
    if (!dt.isValid()) return false;
    _clock.setCurrentTime(dt.unixtime());
    return true;
  }

  uint32_t getCurrentTime() override { return _clock.getCurrentTime(); }
  void tick() override { _clock.tick(); }

  void setCurrentTime(uint32_t time) override {
    _clock.setCurrentTime(time);
    if (!_present) return;
    DateTime dt(time);
    if (dt.year() < 2000 || dt.year() > 2099) return;
    uint8_t control;
    if (!read(0, &control, 1)) {
      MESH_DEBUG_PRINTLN("W10: PCF85063 control read failed");
      return;
    }
    control = (control & ~0x02) | 0x20; // Stop counters while updating and use 24-hour mode
    uint8_t data[] = {bcd(dt.second()), bcd(dt.minute()), bcd(dt.hour()), bcd(dt.day()),
        dt.dayOfTheWeek(), bcd(dt.month()), bcd(dt.year() - 2000)};
    if (!write(0, &control, 1) || !write(4, data, sizeof(data))) {
      MESH_DEBUG_PRINTLN("W10: PCF85063 time write failed; using system clock");
      return; // Leave STOP set after a partial write so the next boot can't trust it
    }
    control &= ~0x20;
    if (!write(0, &control, 1)) MESH_DEBUG_PRINTLN("W10: PCF85063 restart failed");
  }
};
