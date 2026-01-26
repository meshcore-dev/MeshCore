#include "PowerManager.h"
#include <cmath>
#include <cstring>
#include <helpers/ESP32Board.h>
#include <MeshCore.h>

#if ENV_INCLUDE_MAX17261
#include "gauges/Max17261Gauge.h"
#endif

// Fallback ADC gauge using the board's getBattMilliVolts and no temperature
class AdcFallbackGauge : public BatteryGauge {
public:
  explicit AdcFallbackGauge(mesh::MainBoard &board) : boardRef(board) {}
  bool probe(TwoWire &wire) override { return true; }
  bool begin(TwoWire &wire) override { return true; }
  uint16_t readMillivolts() override { return boardRef.getBattMilliVolts(); }
  float readBatteryTemperatureC() override { return NAN; }
private:
  mesh::MainBoard &boardRef;
};

PowerManager::PowerManager()
  : activeGauge(nullptr), wireRef(nullptr), voltageCache{0, 0}, tempCache{0, NAN} {
}

PowerManager::~PowerManager() {
  delete activeGauge;
}

void PowerManager::begin(mesh::MainBoard &board, TwoWire &wire) {
  wireRef = &wire;
  selectGauge(board);
}

void PowerManager::selectGauge(mesh::MainBoard &board) {
  // Try supported gauges in priority order
  // keep allocations minimal
  (void)board;

#if ENV_INCLUDE_MAX17261
  if (BatteryGauge* g = createMax17261GaugeIfPresent(*wireRef)) {
    activeGauge = g;
    return;
  }
#endif

  // Default fallback
  activeGauge = new AdcFallbackGauge(board);
}

uint16_t PowerManager::getVoltageMv() {
  uint32_t now = millis();
  if (voltageCache.cachedValue != 0 && (now - voltageCache.lastReadMs) < 1000) {
    return voltageCache.cachedValue;
  }
  if (!activeGauge) return 0;
  voltageCache.cachedValue = activeGauge->readMillivolts();
  voltageCache.lastReadMs = now;
  return voltageCache.cachedValue;
}

float PowerManager::getBattTemperatureC() {
  uint32_t now = millis();
  if (!isnan(tempCache.cachedValue) && (now - tempCache.lastReadMs) < 1000) {
    return tempCache.cachedValue;
  }
  if (!activeGauge) return NAN;
  tempCache.cachedValue = activeGauge->readBatteryTemperatureC();
  tempCache.lastReadMs = now;
  return tempCache.cachedValue;
}


