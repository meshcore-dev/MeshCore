/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace inhero {

// nRF52 hardware watchdog wrappers. The nRF52 WDT cannot be stopped once
// started; disable() only stops the feed loop so the next CRV expiry resets
// the chip. All three are no-ops when DEBUG_MODE is defined.
//
// setupWatchdog(true) blinks LED_BLUE three times as visual confirmation.
void setupWatchdog(bool blinkLed);
void feedWatchdog();
void disableWatchdog();

} // namespace inhero
