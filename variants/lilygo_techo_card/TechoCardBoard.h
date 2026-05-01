#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include "variant.h"

#if defined(HAS_RGB_LED)
  #include <Adafruit_NeoPixel.h>
#endif

// Hard-iron offsets + soft-iron axis scaling.
// Computed by on-device calibration (rotate slowly for ~20 seconds).
// Persisted to /compass_cal on InternalFS.
#define COMPASS_CAL_MAGIC 0xCA1B0000

struct CompassCalibration {
  int16_t off_x, off_y, off_z;      // hard-iron offsets (raw ADC counts)
  float   scale_x, scale_y, scale_z; // soft-iron per-axis scale factors
  uint32_t magic;                     // COMPASS_CAL_MAGIC when valid
};

class TechoCardBoard : public NRF52BoardDCDC {
private:
  #if defined(HAS_RGB_LED)
    Adafruit_NeoPixel _pixels = Adafruit_NeoPixel(NUM_NEOPIXELS, PIN_RGB_LED_1, NEO_GRB + NEO_KHZ800);
  #endif

public:
  TechoCardBoard() : NRF52Board("TECHO_CARD_OTA") {}

  void begin();

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    pinMode(PIN_BAT_CTL, OUTPUT);
    pinMode(PIN_VBAT_READ, INPUT);
    digitalWrite(PIN_BAT_CTL, HIGH);

    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    digitalWrite(PIN_BAT_CTL, LOW);

    return (uint16_t)((float)adcvalue * MV_LSB * ADC_MULTIPLIER);
  }

  const char* getManufacturerName() const override {
    return "LilyGo T-Echo Card";
  }

  float getMCUTemperature() override;

  void powerOff() override {
    sd_power_system_off();
  }

  // GPS power control
  void enableGPS(bool enable);

  // Speaker power control
  void enableSpeaker(bool enable);

  // RGB LEDs -- all three to same colour
  void setLED(uint8_t r, uint8_t g, uint8_t b);
  void ledOff();

  // Per-LED status control (0=power, 1=notify, 2=pairing)
  void setStatusLED(uint8_t led_index, uint32_t color);

  // Buzzer
  void buzz(uint16_t freq_hz, uint16_t duration_ms);

  // BQ25896 charger IC (0x6B)
  bool probeCharger(); // check if BQ25896 responds on I2C
  uint8_t readChargerReg(uint8_t reg);
  void writeChargerReg(uint8_t reg, uint8_t val);
  void enableChargerADC(); // start continuous ADC conversion
  uint8_t getChargeStatus(); // 0=none, 1=pre, 2=fast, 3=done
  uint16_t getChargerBattMV(); // battery voltage from charger ADC
  uint8_t getChargerTSPCT(); // thermistor voltage as % of REGN

  // ICM20948 / AK09916 compass (0x68 bypass to 0x0C)
  bool initCompass();
  bool readMag(int16_t& mx, int16_t& my, int16_t& mz);
  void sleepCompass(); // power down magnetometer + put ICM20948 in sleep mode

  // Compass calibration (persisted to InternalFS)
  bool loadCalibration();   // call after InternalFS.begin()
  bool saveCalibration(const CompassCalibration& cal);
  bool isCalibrated() const { return _cal.magic == COMPASS_CAL_MAGIC; }
  const CompassCalibration& getCalibration() const { return _cal; }

private:
  bool _compassReady = false;
  bool _chargerProbed = false;
  bool _chargerPresent = false;
  CompassCalibration _cal = { 0, 0, 0, 1.0f, 1.0f, 1.0f, 0 };
};