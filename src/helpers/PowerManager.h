#pragma once

#include <stdint.h>
#include <Arduino.h>
#include <Wire.h>

// Forward declarations
namespace mesh { class MainBoard; }

template<typename T>
struct TimedCache {
  uint32_t lastReadMs;
  T cachedValue;
};

// Minimal battery gauge interface
class BatteryGauge {
public:
  virtual ~BatteryGauge() {}
  virtual bool probe(TwoWire &wire) = 0;       // Detect presence on I2C
  virtual bool begin(TwoWire &wire) = 0;       // Initialize device
  virtual uint16_t readMillivolts() = 0;       // Battery voltage in mV
  virtual float readBatteryTemperatureC() = 0; // Battery (or die) temperature in C (NAN if unavailable)
};

// Lightweight power manager with pluggable gauges
class PowerManager {
public:
  PowerManager();
  ~PowerManager();

  // Attempts to detect a supported gauge on I2C and use it, otherwise falls back to ADC
  void begin(mesh::MainBoard &board, TwoWire &wire);

  // Cached at ~1s cadence to limit I2C traffic
  uint16_t getVoltageMv();
  float getBattTemperatureC();

private:
  BatteryGauge *activeGauge;
  TwoWire *wireRef;

  // Simple cache
  TimedCache<uint16_t> voltageCache;
  TimedCache<float> tempCache;

  // Internal helpers
  void selectGauge(mesh::MainBoard &board);
};


