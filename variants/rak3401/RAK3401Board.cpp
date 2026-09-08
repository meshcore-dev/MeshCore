#include <Arduino.h>
#include <Wire.h>

#include "RAK3401Board.h"

#ifdef NRF52_POWER_MANAGEMENT

// Analog (LPCOMP) wake source for SYSTEMOFF. Precedence: an explicit
// PIN_WAKEUP_ANA wins, else the AIN user button. Lets a build point the wake at
// a different AIN pin than the button (e.g. an INT line wired to that pin)
// without touching the button define. The pin must idle near VDD and pull low
// on activity, and must be analog-capable: nRF52840 AIN map is
// P0.02-05 = AIN0-3, P0.28-31 = AIN4-7.
#if defined(PIN_WAKEUP_ANA)
  #define PWRMGT_WAKE_PIN  PIN_WAKEUP_ANA
#elif defined(PIN_USER_BTN_ANA)
  #define PWRMGT_WAKE_PIN  PIN_USER_BTN_ANA
#endif

#if defined(PWRMGT_WAKE_PIN)
  #ifndef PWRMGT_WAKE_AIN
    #define PWRMGT_WAKE_AIN  ((PWRMGT_WAKE_PIN) >= 28 ? (PWRMGT_WAKE_PIN) - 24 : (PWRMGT_WAKE_PIN) - 2)
  #endif
  #ifndef PWRMGT_WAKE_REFSEL
    #define PWRMGT_WAKE_REFSEL  3   // 4/8 VDD (~1.65V at 3.3V) threshold
  #endif
#endif

// Static configuration for power management
// Values set in variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void RAK3401Board::initiateShutdown(uint8_t reason) {
  // Disable SKY66122 FEM (CSD+CPS LOW = shutdown, <1 uA)
  digitalWrite(SX126X_POWER_EN, LOW);

  // Disable 3V3 switched peripherals and 5V boost
  digitalWrite(PIN_3V3_EN, LOW);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

#if defined(PWRMGT_WAKE_PIN)
  // Wake-from-SYSTEMOFF on the analog wake pin (P0.31 = AIN7 by default): the AIN
  // user button, or whatever PIN_WAKEUP_ANA points at.
  //
  // The button is wired as an *analog* button (see MomentaryButton in target.cpp:
  // pressed == analogRead() < threshold). GPIO SENSE can't be used as the wake
  // source: using the pin as an analog/SAADC input leaves its digital input
  // buffer disconnected, so NRF_GPIO->IN reads 0 regardless of the real ~VDD
  // level and SENSE_Low latches immediately (verified on hardware — analogRead
  // reports ~VDD while IN=0). A GPIO SENSE arm therefore wakes the chip the
  // instant we enter SYSTEMOFF and it can never stay off.
  //
  // LPCOMP works in the analog domain, so it sees the idle level correctly. Arm
  // it for a DOWN crossing at ~1/2 VDD: released idles near VDD (above), a press
  // pulls the pin toward 0V (below) -> downward crossing -> wake. The LPCOMP is
  // otherwise unused for a USER shutdown (voltage wake is only armed for the
  // low-voltage / boot-protect reasons handled above), so there is no conflict.
  //
  // Wait for release first so LPCOMP is armed while the level is above the
  // threshold — otherwise the initial press generates no new downward crossing.
  // Bounded by a timeout so a stuck/low reading can never wedge shutdown. (A pin
  // driven by an idle-high INT line should be drained to idle by the caller
  // before shutdown, since this level wait can't observe its release.)
  analogReadResolution(12);  // as getBattMilliVolts() does; makes the threshold below unambiguous
  const int WAKE_RELEASED_ADC = 2048;  // mid-scale at 12-bit: above the ~1/2 VDD LPCOMP threshold
  uint32_t t0 = millis();
  int released_streak = 0;
  while (released_streak < 5 && (millis() - t0) < 5000) {
    if (analogRead(PWRMGT_WAKE_PIN) > WAKE_RELEASED_ADC) released_streak++;
    else released_streak = 0;
    delay(10);
  }

  configureVoltageWake(PWRMGT_WAKE_AIN, PWRMGT_WAKE_REFSEL, /*detect_down=*/true);
#endif

  enterSystemOff(reason);
}
#endif

void RAK3401Board::begin() {
  NRF52BoardDCDC::begin();
  pinMode(PIN_VBAT_READ, INPUT);
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  // PIN_3V3_EN (WB_IO2, P0.34) controls the 3V3_S switched peripheral rail
  // AND the 5V boost regulator (U5) on the RAK13302 that powers the SKY66122 PA.
  // Must stay HIGH during radio operation — do not toggle for power saving.
  pinMode(PIN_3V3_EN, OUTPUT);
  digitalWrite(PIN_3V3_EN, HIGH);

  // Enable SKY66122-11 FEM on the RAK13302 module.
  // CSD and CPS are tied together on the RAK13302 PCB, routed to IO3 (P0.21).
  // HIGH = FEM active (LNA for RX, PA path available for TX).
  // TX/RX switching (CTX) is handled by SX1262 DIO2 via SetDIO2AsRfSwitchCtrl.
  pinMode(SX126X_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
  // Boot voltage protection check (may not return if voltage too low)
  // We need to call this after we configure SX126X_POWER_EN as output but before we pull high
  checkBootVoltage(&power_config);
#endif
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(1);  // SKY66122 turn-on settling time (tON = 3us typ)
}
