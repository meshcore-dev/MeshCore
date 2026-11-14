/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Arduino.h>

namespace inhero {

// Maps a SOC percentage (0-100%) to a fake Li-Ion 1S OCV in millivolts.
// Uses a standard Li-Ion NMC/NCA OCV lookup table with piecewise-linear
// interpolation. The companion app reverse-maps these voltages back to the
// same SOC%, giving a correct battery-level display regardless of the
// actual cell chemistry (Li-Ion, LiFePO4, LTO, Na-Ion).
uint16_t socToLiIonMilliVolts(float soc_percent);

} // namespace inhero
