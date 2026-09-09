/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "BatteryOcvMapping.h"

namespace inhero {

uint16_t socToLiIonMilliVolts(float soc_percent) {
  // Clamp input to valid range
  if (soc_percent <= 0.0f) return 3000;
  if (soc_percent >= 100.0f) return 4200;

  // Standard Li-Ion 1S OCV table (NMC/NCA, 10% steps).
  // Index 0 = 0% SOC, Index 10 = 100% SOC.
  static const uint16_t LI_ION_OCV_TABLE[] = {
    3000,  // 0%
    3300,  // 10%
    3450,  // 20%
    3530,  // 30%
    3600,  // 40%
    3670,  // 50%
    3740,  // 60%
    3820,  // 70%
    3920,  // 80%
    4050,  // 90%
    4200   // 100%
  };

  // Piecewise-linear interpolation between 10% steps.
  float index_f = soc_percent / 10.0f;       // 0.0 – 10.0
  uint8_t idx_lo = (uint8_t)index_f;
  if (idx_lo >= 10) idx_lo = 9;              // safety clamp
  uint8_t idx_hi = idx_lo + 1;

  float frac = index_f - (float)idx_lo;
  float mv = (float)LI_ION_OCV_TABLE[idx_lo]
           + frac * (float)(LI_ION_OCV_TABLE[idx_hi] - LI_ION_OCV_TABLE[idx_lo]);

  return (uint16_t)(mv + 0.5f);              // round to nearest mV
}

} // namespace inhero
