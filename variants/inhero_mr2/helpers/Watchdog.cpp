/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "Watchdog.h"

#include <Arduino.h>
#include <MeshCore.h>
#include <nrf_wdt.h>

namespace inhero {

static bool s_wdtEnabled = false;

void setupWatchdog(bool blinkLed) {
#ifndef DEBUG_MODE
  NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) |
                    (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);
  NRF_WDT->CRV = 32768 * 600;  // 600 s @ 32.768 kHz - long enough for OTA
  NRF_WDT->RREN = WDT_RREN_RR0_Enabled << WDT_RREN_RR0_Pos;
  NRF_WDT->TASKS_START = 1;
  s_wdtEnabled = true;
  MESH_DEBUG_PRINTLN("Watchdog enabled: 600s timeout");

#ifdef LED_BLUE
  if (blinkLed) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BLUE, HIGH);
      delay(100);
      digitalWrite(LED_BLUE, LOW);
      delay(100);
    }
  }
#else
  (void)blinkLed;
#endif
#else
  (void)blinkLed;
  MESH_DEBUG_PRINTLN("Watchdog disabled (DEBUG_MODE)");
#endif
}

void feedWatchdog() {
#ifndef DEBUG_MODE
  if (s_wdtEnabled) {
    NRF_WDT->RR[0] = WDT_RR_RR_Reload;
  }
#endif
}

void disableWatchdog() {
#ifndef DEBUG_MODE
  // nRF52 WDT cannot be stopped once started -- only stop feeding.
  s_wdtEnabled = false;
#endif
}

} // namespace inhero
