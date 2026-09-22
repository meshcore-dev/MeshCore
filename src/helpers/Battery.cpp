#include <stddef.h>
#include "Battery.h"

// Build the OCV table from the configured macro.
// 11 entries: 100%, 90%, ..., 0%.
static const uint16_t kOcvTable[] = { OCV_ARRAY };
static const size_t kOcvTableSize = sizeof(kOcvTable) / sizeof(kOcvTable[0]);

#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert(kOcvTableSize == 11,
              "OCV_ARRAY must contain exactly 11 entries: 100%, 90%, ..., 0%.");
#endif

int batteryPercentFromMilliVolts(uint16_t batteryMilliVolts) {
  const uint8_t stepPct = 10;         // distance between table entries (100 → 90 → ... → 0)
  const size_t n = kOcvTableSize;     // should be 11

  if (NUM_CELLS_IN_SERIES > 1) {
    // Adjust the input voltage to per-cell basis
    batteryMilliVolts /= NUM_CELLS_IN_SERIES;
  }

  if (n != 11 || batteryMilliVolts <= 0) {
    // Error: invalid OCV_ARRAY table size or voltage
    return -1;
  }

  // Above or equal to "full" voltage → clamp to 100%
  if (batteryMilliVolts >= kOcvTable[0]) {
    return 100;
  }

  // Below or equal to "empty" voltage → clamp to 0%
  if (batteryMilliVolts <= kOcvTable[n - 1]) {
    return 0;
  }

  // Find the segment [i, i+1] where:
  //   vHigh >= batteryMilliVolts >= vLow
  // and map that to [pctHigh, pctLow] = [100 - 10*i, 100 - 10*(i+1)]
  for (size_t i = 0; i < n - 1; ++i) {
    uint16_t vHigh = kOcvTable[i];       // higher voltage, higher %
    uint16_t vLow  = kOcvTable[i + 1];   // lower voltage, lower %

    if (batteryMilliVolts <= vHigh && batteryMilliVolts >= vLow) {
      uint8_t pctHigh = 100 - i * stepPct;
      uint8_t pctLow  = 100 - (i + 1) * stepPct;

      uint16_t dv = vHigh - vLow;
      if (dv == 0) {
        return pctLow;
      }

      // How far are we from the low voltage towards the high, as a fraction of the segment?
      uint16_t pv = batteryMilliVolts - vLow;  // in [0, dv]

      // Interpolate percentage within this 10% band.
      uint8_t deltaPct = (uint32_t)pv * stepPct / dv;
      int pct = pctLow + deltaPct;

      // Clamp to [0, 100]
      if (pct < 0)   pct = 0;
      if (pct > 100) pct = 100;
      return pct;
    }
  }

  // Should be unreachable if the table is monotonic and cases above are handled.
  return -1;
}
