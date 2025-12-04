#pragma once

#include <Wire.h>
#include "../PowerManager.h"

// Factory for MAX17261 gauge. Returns nullptr if not present or not enabled.
#if ENV_INCLUDE_MAX17261
BatteryGauge* createMax17261GaugeIfPresent(TwoWire &wire);
#else
inline BatteryGauge* createMax17261GaugeIfPresent(TwoWire &) { return nullptr; }
#endif


