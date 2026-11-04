/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "UsbAutoManagement.h"

#include <Arduino.h>
#include <MeshCore.h>
#include <nrf.h>

#include "../BoardConfigContainer.h"

namespace inhero {

// USB starts enabled (Serial.begin in main)
static bool s_usbActive = true;

bool isUsbPowered() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

void disableUsb() {
  if (s_usbActive) {
    Serial.end();
    NRF_USBD->ENABLE = 0;
    s_usbActive = false;
    BoardConfigContainer::setUsbConnected(false);
    MESH_DEBUG_PRINTLN("USB disabled");
  }
}

void enableUsb() {
  if (!s_usbActive) {
    NRF_USBD->ENABLE = 1;
    Serial.begin(115200);
    s_usbActive = true;
    BoardConfigContainer::setUsbConnected(true);
    MESH_DEBUG_PRINTLN("USB enabled");
  }
}

void serviceUsbAutoManagement() {
  // After Serial.end(), Serial.available() returns 0 and Serial.read() returns -1,
  // so no serial guard is needed in the main loop.
  if (!s_usbActive && isUsbPowered()) enableUsb();
  if (s_usbActive && !isUsbPowered()) disableUsb();
}

} // namespace inhero
