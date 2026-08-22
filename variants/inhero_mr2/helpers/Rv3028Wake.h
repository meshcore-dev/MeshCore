/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Arduino.h>

namespace inhero {

// Configures the RV-3028-C7 periodic countdown timer to fire after `minutes`
// at 1/60 Hz, single-shot, with TIE=1 so the INT pin asserts on expiry.
// `minutes` is clamped to [1, 4095] (12-bit timer register).
void configurePeriodicWake(uint16_t minutes);

// Clears the RV-3028 Timer Flag (TF, status bit 3) without touching other bits.
// Read-modify-write: required because System Sleep wake is a reset, so the
// FALLING-edge ISR never sees the RTC event and TF stays latched.
void clearTimerFlag();

} // namespace inhero
