/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

namespace inhero {

// Manually toggles SCL (up to 9 clocks) to release a slave that holds SDA low
// after OTA/warm-reset. Generates a STOP after recovery. Wire.begin() cannot
// do this on its own. Pins are released back to INPUT before returning so the
// Wire library can take them over.
void recoverI2cBus(uint8_t sda, uint8_t scl);

} // namespace inhero
