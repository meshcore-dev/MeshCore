/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

namespace inhero {

// Manages the nRF52 USB peripheral based on VBUS presence so the device can
// safely run from battery without an enumerated USB host. Also keeps
// BoardConfigContainer's USB-connected state in sync (used for IINDPM).
bool isUsbPowered();
void enableUsb();
void disableUsb();

// Call from board tick(); enables/disables USB on VBUS edge.
void serviceUsbAutoManagement();

} // namespace inhero
