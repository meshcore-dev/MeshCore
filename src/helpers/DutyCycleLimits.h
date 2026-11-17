#pragma once

#include <stdint.h>

// Regulatory duty cycle limits, derived from the frequency a node is tuned to.
//
// Only the 863 to 870 MHz SRD band is constrained here, using the sub-band
// table of ETSI EN 300 220-2. Every other frequency is reported as
// DUTY_CYCLE_UNLIMITED, so US, ANZ and any other region keep the behaviour
// they had before this table existed.

#define DUTY_CYCLE_UNLIMITED  100.0f

// Highest duty cycle (percent) allowed on freq_mhz.
float getMaxDutyCyclePercent(float freq_mhz);

// Duty cycle percentage to the airtime budget factor Dispatcher works with,
// where duty_cycle = 1 / (1 + factor).
float dutyCycleToAirtimeFactor(float percent);

// Airtime budget factor a node should use: derived from its frequency while
// dutycycle_auto is set, otherwise the factor its operator configured.
float getEffectiveAirtimeFactor(uint8_t dutycycle_auto, float airtime_factor, float freq_mhz);
