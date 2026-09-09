/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace inhero {

// Prepares INA228 and BQ25798 for nRF52 System Sleep so the analog ICs
// don't burn quiescent current while the MCU is off:
//  - INA228: shutdown ADC (with readback retry), release latched ALERT
//  - BQ25798: disable ADC, mask all interrupts and clear flags so INT goes high-Z
// Wire must be initialised before calling.
void prepareIcsForSystemOff();

} // namespace inhero
