#include "DutyCycleLimits.h"

#define SRD_BAND_START_MHZ    863.0f
#define SRD_BAND_END_MHZ      870.0f

// Applied inside the SRD band to any frequency that no sub-band below covers.
// Those gaps are the alarm and social alarm allocations, which carry their own
// restrictions, so the tightest limit of the band is used rather than none.
#define SRD_BAND_FALLBACK     0.1f

struct SubBand {
  float start_mhz;
  float end_mhz;
  float max_duty_cycle;
};

// ETSI EN 300 220-2, the sub-bands MeshCore presets are tuned to. A frequency
// that sits exactly on a boundary matches the first entry it falls in, which
// is the lower and therefore more restrictive of the two.
static const SubBand SUB_BANDS[] = {
  { 863.0f, 865.0f,   0.1f },
  { 865.0f, 868.0f,   1.0f },
  { 868.0f, 868.6f,   1.0f },
  { 868.7f, 869.2f,   0.1f },
  { 869.4f, 869.65f, 10.0f },
  { 869.7f, 870.0f,   1.0f },
};

float getMaxDutyCyclePercent(float freq_mhz) {
  if (freq_mhz < SRD_BAND_START_MHZ || freq_mhz > SRD_BAND_END_MHZ) {
    return DUTY_CYCLE_UNLIMITED;
  }
  for (int i = 0; i < (int)(sizeof(SUB_BANDS) / sizeof(SUB_BANDS[0])); i++) {
    if (freq_mhz >= SUB_BANDS[i].start_mhz && freq_mhz <= SUB_BANDS[i].end_mhz) {
      return SUB_BANDS[i].max_duty_cycle;
    }
  }
  return SRD_BAND_FALLBACK;
}

float dutyCycleToAirtimeFactor(float percent) {
  return (100.0f / percent) - 1.0f;
}

float getEffectiveAirtimeFactor(uint8_t dutycycle_auto, float airtime_factor, float freq_mhz) {
  if (!dutycycle_auto) return airtime_factor;

  return dutyCycleToAirtimeFactor(getMaxDutyCyclePercent(freq_mhz));
}
