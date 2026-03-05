#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <Wire.h>
#include <helpers/NRF52Board.h>

#define  PIN_VBAT_READ    31
// Voltage divider ratio 1.667 calibrated by simon-muzi (Meshtastic PR #8716)
// Adapted for MeshCore's default 3.6V analog reference
#define  ADC_MULTIPLIER   (3.6 * 1.667 * 1000)

// NCP5623 I2C RGB LED (register format: [RRR|VVVVV])
#define NCP5623_ADDR          0x38
#define NCP5623_REG_SHUTDOWN  0
#define NCP5623_REG_ILED      1
#define NCP5623_REG_PWM0      2  // Blue
#define NCP5623_REG_PWM1      3  // Green
#define NCP5623_REG_PWM2      4  // Red

class R1NeoBoard : public NRF52BoardDCDC {
  bool _has_ncp5623 = false;

  void ncp5623Write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(NCP5623_ADDR);
    Wire.write(((reg & 0x7) << 5) | (val & 0x1f));
    Wire.endTransmission();
  }

  void ncp5623SetColor(uint8_t r, uint8_t g, uint8_t b) {
    ncp5623Write(NCP5623_REG_PWM2, r >> 3);
    ncp5623Write(NCP5623_REG_PWM1, g >> 3);
    ncp5623Write(NCP5623_REG_PWM0, b >> 3);
  }

protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  R1NeoBoard() : NRF52Board("R1NEO_OTA") {}
  void begin();

#ifdef NRF52_POWER_MANAGEMENT
  void powerOff() override {
    if (_has_ncp5623) {
      ncp5623Write(NCP5623_REG_SHUTDOWN, 0);
    }
    initiateShutdown(SHUTDOWN_REASON_USER);
  }
#endif

  void initNCP5623() {
    Wire.beginTransmission(NCP5623_ADDR);
    Wire.write(0x00);
    _has_ncp5623 = (Wire.endTransmission() == 0);
    if (_has_ncp5623) {
      ncp5623Write(NCP5623_REG_ILED, 10);  // moderate current
      ncp5623SetColor(0, 0, 0);
    }
  }

  void updateStatusLED(uint16_t batt_mv) {
    if (!_has_ncp5623) return;
    bool charging = !digitalRead(PIN_BAT_CHG);  // active-low
    if (charging) {
      if (batt_mv > 4150)
        ncp5623SetColor(0, 255, 0);    // green: fully charged
      else
        ncp5623SetColor(255, 40, 0);   // orange: charging
    } else {
      if (batt_mv < 3400)
        ncp5623SetColor(255, 0, 0);    // red: low battery
      else
        ncp5623SetColor(0, 0, 0);      // off: on battery, OK
    }
  }

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    uint16_t mv = (ADC_MULTIPLIER * raw) / 4096;
    updateStatusLED(mv);
    return mv;
  }

  const char* getManufacturerName() const override {
    return "muzi works R1 Neo";
  }
};
