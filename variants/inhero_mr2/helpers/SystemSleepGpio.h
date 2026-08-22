/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace inhero {

// Puts the SX1262 into Cold Sleep and latches NSS/SCK/MOSI to defined levels
// before nRF52 System Sleep.
//   radioInitialized=true  -> uses RadioLib (radio_driver.powerOff()).
//   radioInitialized=false -> bit-bangs SetSleep on P_LORA_* directly.
// SPI.end() must NOT be called: floating SCK during the disconnect window
// re-wakes the SX1262 (~600uA Standby RC).
void prepareRadioForSystemOff(bool radioInitialized = true);

// Resets every GPIO to INPUT_DISCONNECT/PULL_DISABLED except the few pins
// the design must keep alive across System Sleep:
//   P0.04 (BQ_CE_PIN)   -> OUTPUT HIGH (charging stays enabled)
//   P0.17 (RTC_INT_PIN) -> INPUT_PULLUP + SENSE_Low (wake source)
//   P1.10/11/12         -> latched by prepareRadioForSystemOff() (NSS/SCK/MOSI)
// Must run after Wire.end() and after prepareRadioForSystemOff().
void disconnectLeakyPullups();

} // namespace inhero
