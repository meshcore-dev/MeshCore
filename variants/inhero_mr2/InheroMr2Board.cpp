/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * Inhero MR-2 Board Implementation
 */

// Includes
#include "InheroMr2Board.h"

#include "BoardConfigContainer.h"
#include "target.h"

#include <Arduino.h>
#include <Wire.h>
#include <nrf_soc.h>

namespace {

void clearRtcTimerFlag() {
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_STATUS);
  if (Wire.endTransmission(false) != 0) {
    return;
  }

  Wire.requestFrom((uint8_t)RTC_I2C_ADDR, (uint8_t)1);
  if (!Wire.available()) {
    return;
  }

  uint8_t status = Wire.read();
  if ((status & (1 << 3)) == 0) {
    return;
  }

  status &= ~(1 << 3);
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_STATUS);
  Wire.write(status);
  Wire.endTransmission();
}

} // namespace

// Static declarations
static BoardConfigContainer boardConfig;
volatile bool InheroMr2Board::rtc_irq_pending = false;
volatile uint32_t InheroMr2Board::ota_dfu_reset_at = 0;

// USB power auto-management
static bool usb_active = true;  // USB starts enabled (Serial.begin in main)

static bool isUSBPowered() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

static void disableUSB() {
  if (usb_active) {
    Serial.end();
    NRF_USBD->ENABLE = 0;
    usb_active = false;
    BoardConfigContainer::setUsbConnected(false);
    MESH_DEBUG_PRINTLN("USB disabled");
  }
}

static void enableUSB() {
  if (!usb_active) {
    NRF_USBD->ENABLE = 1;
    Serial.begin(115200);
    usb_active = true;
    BoardConfigContainer::setUsbConnected(true);
    MESH_DEBUG_PRINTLN("USB enabled");
  }
}

// ===== Public Methods =====

void InheroMr2Board::begin() {
  // === FAST PATH: RTC wake from low-voltage sleep ===
  // Check GPREGRET2 FIRST — before ANY GPIO setup.
  // Note: System Sleep wake triggers a System-ON reset. The bootloader runs before our code,
  // and the reset clears all PIN_CNF to Input/Disconnect defaults. BSP init() only does
  // OUTSET=0xFFFFFFFF which has no physical effect on Input-configured pins.
  // Therefore we MUST explicitly re-assert any GPIO we need (CE, etc.) in this path.
  uint8_t shutdown_reason = NRF_POWER->GPREGRET2;

  if ((shutdown_reason & 0x03) == SHUTDOWN_REASON_LOW_VOLTAGE) {
    // Minimal I2C setup — only thing we need
#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
    Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif
    Wire.begin();
    delay(10);

    // SYSTEMOFF wake is a reset, so the FALLING-edge ISR never sees the RTC event.
    // Clear TF here before we arm RTC_INT pull-up + SENSE again.
    clearRtcTimerFlag();

    // RTC INT: must have SENSE_Low for System Sleep wake-up
    NRF_GPIO->PIN_CNF[RTC_INT_PIN] =
        (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
        (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
        (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
        (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);

    uint16_t vbat_mv = Ina228Driver::readVBATDirect(&Wire, 0x40);
    uint16_t wake_threshold = getLowVoltageWakeThreshold();

    MESH_DEBUG_PRINTLN("LV-Wake: VBAT=%dmV, wake=%dmV", vbat_mv, wake_threshold);

    if (vbat_mv == 0 || vbat_mv < wake_threshold) {
      // Still too low or read failed — go back to sleep immediately.
      // INA228 ADC needs shutdown (readVBATDirect left it in one-shot mode).

      // BQ CE pin: The System-ON reset after System Sleep wake resets all PIN_CNF
      // to Input/Disconnect defaults. The previous cycle's OUTPUT latch is lost.
      // Must explicitly re-assert OUTPUT HIGH so solar charging stays active.
#ifdef BQ_CE_PIN
      pinMode(BQ_CE_PIN, OUTPUT);
      digitalWrite(BQ_CE_PIN, HIGH);
      MESH_DEBUG_PRINTLN("LV-Wake: CE re-latched HIGH (solar charging active)");
#endif

      // INA228 → Shutdown mode with readback verification (~3.5µA vs ~350µA continuous).
      // I2C writes can fail silently — if this fails, 350µA wasted in System Sleep!
      for (int retry = 0; retry < 3; retry++) {
        Wire.beginTransmission(0x40);
        Wire.write(0x01);  // ADC_CONFIG register
        Wire.write(0x00);  // Shutdown mode (MSB)
        Wire.write(0x00);  // (LSB)
        if (Wire.endTransmission() != 0) {
          delay(10);
          continue;
        }
        delay(2);
        // Readback verification
        Wire.beginTransmission(0x40);
        Wire.write(0x01);
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x40, (uint8_t)2);
        uint16_t rb = 0;
        if (Wire.available() >= 2) {
          rb = (Wire.read() << 8) | Wire.read();
        }
        if ((rb & 0xF000) == 0x0000) break;
        delay(10);
      }

      // INA228 — Release ALERT pin before sleep.
      // The under-voltage alert is latched (ALATCH=1) → ALERT stays LOW.
      // RAK4630 internal pull-up on P1.02 → ~330µA through pull-up into OD transistor.
      // 1. Write DIAG_ALRT=0: ALATCH=0 (transparent mode) + clear all flag bits
      Wire.beginTransmission(0x40);
      Wire.write(0x0B);  // DIAG_ALRT register
      Wire.write(0x00);  // MSB: ALATCH=0, CNVR=0, SLOWALERT=0, APOL=0
      Wire.write(0x00);  // LSB: clear all flags
      Wire.endTransmission();
      // 2. Set BUVL=0 (no threshold → no alert condition in transparent mode)
      Wire.beginTransmission(0x40);
      Wire.write(0x08);  // BUVL register
      Wire.write(0x00);
      Wire.write(0x00);
      Wire.endTransmission();

      // BQ25798 — Disable ADC (saves ~500µA continuous draw)
      Wire.beginTransmission(BQ25798_I2C_ADDR);
      Wire.write(0x2E);  // ADC_CONTROL register
      Wire.write(0x00);  // ADC_EN=0, ADC disabled
      Wire.endTransmission();

      // BQ25798 — Mask all interrupts + clear flags to de-assert INT pin.
      // Without this, any pending flag holds INT LOW and INPUT_PULLUP wastes ~254µA.
      { const uint8_t mask_regs[] = {0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D};
        for (uint8_t r : mask_regs) {
          Wire.beginTransmission(BQ25798_I2C_ADDR);
          Wire.write(r);
          Wire.write(0xFF);  // Mask all
          Wire.endTransmission();
        }
        const uint8_t flag_regs[] = {0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
        for (uint8_t r : flag_regs) {
          Wire.beginTransmission(BQ25798_I2C_ADDR);
          Wire.write(r);
          Wire.endTransmission(false);
          Wire.requestFrom((uint8_t)BQ25798_I2C_ADDR, (uint8_t)1);
          while (Wire.available()) Wire.read();
        }
      }

      // SX1262: Send SetSleep command AND latch NSS HIGH.
      // After System-ON reset, SX1262 may be in Standby RC (~600µA).
      // Both are needed: SetSleep puts it to Cold Sleep, NSS latch prevents re-wake.
      prepareRadioForSystemOff(false);

      configureRTCWake(LOW_VOLTAGE_SLEEP_MINUTES);
      NRF_P0->LATCH = (1UL << RTC_INT_PIN);
      Wire.end();

      // Disconnect all GPIO pull-ups to prevent leakage in System Sleep.
      // Wire.begin() set SDA/SCL to INPUT_PULLUP; if an I2C device holds
      // the line LOW, each pull-up wastes ~250µA. Wire.end() alone does NOT
      // disable the pull-ups on nRF52.
      disconnectLeakyPullups();

      NRF_POWER->GPREGRET2 = GPREGRET2_LOW_VOLTAGE_SLEEP | SHUTDOWN_REASON_LOW_VOLTAGE;
      sd_power_system_off();
      NRF_POWER->SYSTEMOFF = 1;
      while (1) __WFE();
    }

    // Voltage recovered — close I2C and fall through to normal boot
    Wire.end();

    // Recovery LED flash
    pinMode(LED_BLUE, OUTPUT);
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BLUE, HIGH);
      delay(150);
      digitalWrite(LED_BLUE, LOW);
      delay(150);
    }

    NRF_POWER->GPREGRET2 = SHUTDOWN_REASON_NONE;
    // setLowVoltageRecovery + setSOCManually deferred to after boardConfig.begin()
    MESH_DEBUG_PRINTLN("LV-Wake: Voltage recovered (%dmV >= %dmV) — normal boot", vbat_mv, wake_threshold);
  }

  // === Standard boot path (ColdBoot, recovery, or non-LV wake) ===
  bool isLowVoltageRecovery = ((shutdown_reason & 0x03) == SHUTDOWN_REASON_LOW_VOLTAGE);

  pinMode(PIN_VBAT_READ, INPUT);

  // BQ25798 CE pin: Drive LOW on every boot so the external FET stays OFF.
  // Rev 1.1: DMN2004TK-7 FET inverts logic — LOW on GPIO = FET off = CE released.
  // The external CE pull-up is 100k to REGN, so CE goes HIGH and charging stays disabled only when REGN is present.
  // configureChemistry() will drive HIGH only after successful I2C configuration with a known battery type.
#ifdef BQ_CE_PIN
  pinMode(BQ_CE_PIN, OUTPUT);
  digitalWrite(BQ_CE_PIN, LOW);
#endif

  // PE4259 RF switch power enable (VDD pin 6 on PE4259)
  // P1.05 (GPIO 37) supplies VDD to the PE4259 SPDT antenna switch on the RAK4630.
  // DIO2 of the SX1262 controls the CTRL pin (pin 4) for TX/RX path selection.
  // Without VDD, the RF switch cannot operate and no TX/RX is possible.
  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10); // Give PE4259 time to power up

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  // === I2C Bus Recovery ===
  // After OTA/warm-reset, a slave may hold SDA low (stuck mid-transaction).
  // Wire.begin() cannot recover this — toggle SCL manually to release the bus.
  // Standard recovery: up to 9 clock pulses while SDA is monitored.
  {
    const uint8_t sda = PIN_BOARD_SDA;
    const uint8_t scl = PIN_BOARD_SCL;

    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, OUTPUT);
    digitalWrite(scl, HIGH);

    bool bus_stuck = (digitalRead(sda) == LOW);
    if (bus_stuck) {
      for (int i = 0; i < 9; i++) {
        digitalWrite(scl, LOW);
        delayMicroseconds(5);
        digitalWrite(scl, HIGH);
        delayMicroseconds(5);
        if (digitalRead(sda) == HIGH) break;  // SDA released
      }
      // Generate STOP condition: SDA LOW→HIGH while SCL is HIGH
      pinMode(sda, OUTPUT);
      digitalWrite(sda, LOW);
      delayMicroseconds(5);
      digitalWrite(scl, HIGH);
      delayMicroseconds(5);
      digitalWrite(sda, HIGH);
      delayMicroseconds(5);
      MESH_DEBUG_PRINTLN("I2C bus recovery performed (SDA was stuck LOW)");
    }
    // Release pins for Wire library
    pinMode(sda, INPUT);
    pinMode(scl, INPUT);
  }

  Wire.begin();
  delay(50); // Give I2C bus time to stabilize

  // MR2 Rev 1.1 hardware — no detection needed
  MESH_DEBUG_PRINTLN("Inhero MR2 - Hardware Rev 1.1 (INA228 ALERT + RTC + CE-FET)");

  // === CRITICAL: Configure RTC INT pin for wake-up from System Sleep ===
  // attachInterrupt() alone is NOT sufficient for System Sleep wake-up!
  // We MUST configure the pin with SENSE for nRF52 SYSTEMOFF wake capability
  pinMode(RTC_INT_PIN, INPUT_PULLUP);

  // Configure GPIO SENSE for wake-up from System Sleep (nRF52 SYSTEMOFF mode)
  // This is essential - without SENSE configuration, System Sleep wake-up will not work
  NRF_GPIO->PIN_CNF[RTC_INT_PIN] =
      (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
      (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
      (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
      (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
      (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos); // Wake on LOW (RTC interrupt is active-low)

  attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN), rtcInterruptHandler, FALLING);

  // === Early Boot Voltage Check (ColdBoot only) ===
  // LV-wake resleep is handled by the fast path above.
  // This section handles ColdBoot below sleep threshold and normal ColdBoot.

  if (!isLowVoltageRecovery) {
    MESH_DEBUG_PRINTLN("Early Boot: Reading VBAT from INA228 @ 0x40...");
    uint16_t vbat_mv = Ina228Driver::readVBATDirect(&Wire, 0x40);
    MESH_DEBUG_PRINTLN("Early Boot: readVBATDirect returned %dmV", vbat_mv);

    if (vbat_mv == 0) {
      MESH_DEBUG_PRINTLN("Early Boot: Failed to read battery voltage, assuming OK");
    } else {
      BoardConfigContainer::BatteryType bootBatType = boardConfig.getBatteryType();
      uint16_t wake_threshold = getLowVoltageWakeThreshold();
      uint16_t sleep_threshold = getLowVoltageSleepThreshold();

      MESH_DEBUG_PRINTLN("Early Boot Check: VBAT=%dmV, Wake=%dmV (0%% SOC), Sleep=%dmV, Reason=0x%02X",
                         vbat_mv, wake_threshold, sleep_threshold, shutdown_reason);

      if (bootBatType == BoardConfigContainer::BAT_UNKNOWN) {
        MESH_DEBUG_PRINTLN("Early Boot: BAT_UNKNOWN - skipping low-voltage check (configure battery type first)");
        if ((shutdown_reason & 0x03) == SHUTDOWN_REASON_LOW_VOLTAGE ||
            (shutdown_reason & GPREGRET2_LOW_VOLTAGE_SLEEP)) {
          NRF_POWER->GPREGRET2 = SHUTDOWN_REASON_NONE;
          MESH_DEBUG_PRINTLN("Early Boot: Cleared stale GPREGRET2 flags (was 0x%02X)", shutdown_reason);
        }
      }
      // ColdBoot with voltage below sleep threshold — first entry into LV sleep
      else if (vbat_mv < sleep_threshold) {
        MESH_DEBUG_PRINTLN("ColdBoot below sleep threshold (%dmV < %dmV)", vbat_mv, sleep_threshold);
        MESH_DEBUG_PRINTLN("Going to sleep for %d min to avoid motorboating", LOW_VOLTAGE_SLEEP_MINUTES);

        delay(100);
        prepareRadioForSystemOff(false);

        // INA228 → Shutdown mode with readback verification
        for (int retry = 0; retry < 3; retry++) {
          Wire.beginTransmission(0x40);
          Wire.write(0x01);  // ADC_CONFIG register
          Wire.write(0x00);  // Shutdown (MSB)
          Wire.write(0x00);  // (LSB)
          if (Wire.endTransmission() != 0) {
            delay(10);
            continue;
          }
          delay(2);
          Wire.beginTransmission(0x40);
          Wire.write(0x01);
          Wire.endTransmission(false);
          Wire.requestFrom((uint8_t)0x40, (uint8_t)2);
          uint16_t rb = 0;
          if (Wire.available() >= 2) {
            rb = (Wire.read() << 8) | Wire.read();
          }
          if ((rb & 0xF000) == 0x0000) break;
          delay(10);
        }

        // Read DIAG_ALRT to clear any latched alert flag
        Wire.beginTransmission(0x40);
        Wire.write(0x0B);
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)0x40, (uint8_t)2);
        while (Wire.available()) Wire.read();

        // Latch BQ CE pin HIGH (solar charging active in sleep)
#ifdef BQ_CE_PIN
        digitalWrite(BQ_CE_PIN, HIGH);
#endif

        // BQ25798 — Disable ADC (saves ~500µA continuous draw)
        Wire.beginTransmission(BQ25798_I2C_ADDR);
        Wire.write(0x2E);  // ADC_CONTROL
        Wire.write(0x00);  // ADC_EN=0
        Wire.endTransmission();

        // BQ25798 — Mask all interrupts + clear flags to de-assert INT
        { const uint8_t mask_regs[] = {0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D};
          for (uint8_t r : mask_regs) {
            Wire.beginTransmission(BQ25798_I2C_ADDR);
            Wire.write(r);
            Wire.write(0xFF);
            Wire.endTransmission();
          }
          const uint8_t flag_regs[] = {0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
          for (uint8_t r : flag_regs) {
            Wire.beginTransmission(BQ25798_I2C_ADDR);
            Wire.write(r);
            Wire.endTransmission(false);
            Wire.requestFrom((uint8_t)BQ25798_I2C_ADDR, (uint8_t)1);
            while (Wire.available()) Wire.read();
          }
        }

        // BME280 — Force Sleep mode
        Wire.beginTransmission(0x76);
        Wire.write(0xF4);  // ctrl_meas
        Wire.write(0x00);  // Sleep mode
        Wire.endTransmission();

        configureRTCWake(LOW_VOLTAGE_SLEEP_MINUTES);
        NRF_P0->LATCH = (1UL << RTC_INT_PIN);

        Wire.end();
        disconnectLeakyPullups();
        NRF_POWER->GPREGRET2 = GPREGRET2_LOW_VOLTAGE_SLEEP | SHUTDOWN_REASON_LOW_VOLTAGE;

        sd_power_system_off();
        NRF_POWER->SYSTEMOFF = 1;
        while (1) __WFE();
      }
      // Normal ColdBoot — voltage OK
      else {
        MESH_DEBUG_PRINTLN("Normal ColdBoot - voltage OK (%dmV >= %dmV)", vbat_mv, sleep_threshold);
      }
    }
  }

  // === Normal boot path: Initialize board hardware ===
  // Only reached when voltage is OK (or unreadable) — resleep paths exit above.
  // boardConfig.begin() initializes BQ25798, INA228, CE pin, alerts, LEDs, etc.
  MESH_DEBUG_PRINTLN("Initializing Rev 1.1 features (BQ25798, INA228, RTC, CE-FET)");
  boardConfig.begin();

  // Handle low-voltage recovery (deferred until after boardConfig.begin())
  if (isLowVoltageRecovery) {
    boardConfig.setLowVoltageRecovery();
    BoardConfigContainer::setSOCManually(0.0f);
    MESH_DEBUG_PRINTLN("SOC: Set to 0%% (low-voltage recovery)");
  }

  // Enable DC/DC converter REG1 for improved power efficiency (~1.5mA savings)
  // REG1: VDD 3.3V → 1.3V core (DC/DC vs LDO)
  // REG0 (DCDCEN0) is NOT needed — RAK4630 is powered from TPS62840 3.3V rail (VDD),
  // not from VBUS (USB). REG0 only applies to the VBUS→VDD_nRF path.
  // Done after peripheral initialization to avoid voltage glitches
  NRF52BoardDCDC::begin();

  // LEDs already initialized in boardConfig.begin()
  // Blue LED was used for boot sequence visualization
  // Red LED indicates missing components (if blinking)

  // Start hardware watchdog (600s timeout)
  // Must be last - after all initializations are complete
  BoardConfigContainer::setupWatchdog();

  // Set initial USB IINDPM limit based on VBUS state at boot
  if (isUSBPowered()) {
    BoardConfigContainer::setUsbConnected(true);
  }
}

void InheroMr2Board::tick() {
  // USB auto-management: enable/disable USB peripheral based on VBUS presence.
  // After Serial.end(), Serial.available() returns 0 and Serial.read() returns -1,
  // so no serial guard is needed in the main loop.
  if (!usb_active && isUSBPowered()) {
    enableUSB();
  }
  if (usb_active && !isUSBPowered()) {
    disableUSB();
  }

  // Deferred OTA DFU reset: wait for CLI reply to be sent, then enter bootloader
  if (ota_dfu_reset_at != 0 && millis() >= ota_dfu_reset_at) {
    enterOTADfu();  // disables SoftDevice & interrupts, sets GPREGRET, resets — does not return
  }

  if (rtc_irq_pending) {
    rtc_irq_pending = false;

    // Clear TF here (not in ISR) to avoid I2C bus collisions with core RTC access.
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_STATUS);
    Wire.endTransmission(false);
    Wire.requestFrom(RTC_I2C_ADDR, (uint8_t)1);

    if (Wire.available()) {
      uint8_t status = Wire.read();
      status &= ~(1 << 3); // Clear TF bit (bit 3)

      Wire.beginTransmission(RTC_I2C_ADDR);
      Wire.write(RV3028_REG_STATUS);
      Wire.write(status);
      Wire.endTransmission();
    }
  }

  // Dispatch all periodic I2C work (MPPT, SOC, hourly stats, low-V alert check)
  boardConfig.tickPeriodic();

  // All healthy — feed watchdog at the END (after I2C operations completed successfully)
  BoardConfigContainer::feedWatchdog();

  // Briefly idle via WFE until next interrupt (radio DIO1, SysTick, USB, I2C).
  // Typically wakes within 1ms. Reduces CPU current from ~3mA (busy-loop) to ~0.5-0.8mA.
  // Harmless when powersaving also calls sleep() — on nRF52 both are just WFE.
  sleep(0);
}

uint16_t InheroMr2Board::getBattMilliVolts() {
  // WORKAROUND: The MeshCore protocol currently only transmits battery voltage
  // (via getBattMilliVolts), not a direct SOC percentage. The companion app then
  // interprets this voltage using a hardcoded Li-Ion discharge curve to derive SOC%.
  // This gives wrong readings for LiFePO4/LTO chemistries whose voltage profiles
  // differ significantly from Li-Ion.
  //
  // Solution: When we have a valid Coulomb-counted SOC, we reverse-map it to
  // the Li-Ion 1S OCV (Open Circuit Voltage) that the app expects.
  // This way the app always displays our accurate chemistry-independent SOC.
  //
  // TODO: Remove this workaround once MeshCore supports transmitting the actual
  // SOC percentage alongside (or instead of) battery millivolts. At that point,
  // this function should return the real battery voltage again.

  const BatterySOCStats* socStats = boardConfig.getSOCStats();
  if (socStats && socStats->soc_valid) {
    return socToLiIonMilliVolts(boardConfig.getStateOfCharge());
  }

  // Fallback: no valid Coulomb-counting SOC yet — return real voltage
  const Telemetry* telemetry = boardConfig.getTelemetryData();
  if (!telemetry) {
    return 0;
  }
  return telemetry->batterie.voltage;
}

/// @brief Maps a SOC percentage (0-100%) to a fake Li-Ion 1S OCV in millivolts.
/// @details Uses a standard Li-Ion NMC/NCA OCV lookup table with piecewise-linear
///          interpolation. The companion app will reverse-map these voltages back
///          to the same SOC%, giving correct battery level display regardless of
///          the actual cell chemistry (Li-Ion, LiFePO4, LTO).
/// @param soc_percent State of Charge in percent (0.0 – 100.0)
/// @return Equivalent Li-Ion 1S voltage in millivolts (3000 – 4200)
uint16_t InheroMr2Board::socToLiIonMilliVolts(float soc_percent) {
  // Clamp input to valid range
  if (soc_percent <= 0.0f) return 3000;
  if (soc_percent >= 100.0f) return 4200;

  // Standard Li-Ion 1S OCV table (NMC/NCA, 10% steps)
  // Index 0 = 0% SOC, Index 10 = 100% SOC
  static const uint16_t LI_ION_OCV_TABLE[] = {
    3000,  // 0%
    3300,  // 10%
    3450,  // 20%
    3530,  // 30%
    3600,  // 40%
    3670,  // 50%
    3740,  // 60%
    3820,  // 70%
    3920,  // 80%
    4050,  // 90%
    4200   // 100%
  };

  // Piecewise-linear interpolation between 10% steps
  float index_f = soc_percent / 10.0f;       // 0.0 – 10.0
  uint8_t idx_lo = (uint8_t)index_f;          // lower table index
  if (idx_lo >= 10) idx_lo = 9;               // safety clamp
  uint8_t idx_hi = idx_lo + 1;

  float frac = index_f - (float)idx_lo;       // fractional part (0.0 – 1.0)
  float mv = (float)LI_ION_OCV_TABLE[idx_lo]
           + frac * (float)(LI_ION_OCV_TABLE[idx_hi] - LI_ION_OCV_TABLE[idx_lo]);

  return (uint16_t)(mv + 0.5f);               // round to nearest mV
}

bool InheroMr2Board::startOTAUpdate(const char* id, char reply[]) {
  // Skip the in-app BLE DFU (unstable on nRF52 in MeshCore environment) and
  // jump directly into the Adafruit bootloader's OTA DFU mode.
  // enterOTADfu() sets GPREGRET=0xA8, disables SoftDevice & interrupts, then resets.
  // The bootloader handles BLE advertising and firmware transfer natively.
  MESH_DEBUG_PRINTLN("OTA: Scheduling Adafruit bootloader DFU mode...");

  // Read BLE MAC address from nRF52 hardware registers (no Bluefruit needed)
  uint32_t addr0 = NRF_FICR->DEVICEADDR[0];
  uint32_t addr1 = NRF_FICR->DEVICEADDR[1];
  snprintf(reply, 64, "OK DFU - mac: %02X:%02X:%02X:%02X:%02X:%02X",
           (addr1 >> 8) & 0xFF, addr1 & 0xFF,
           (addr0 >> 24) & 0xFF, (addr0 >> 16) & 0xFF, (addr0 >> 8) & 0xFF, addr0 & 0xFF);

  // Schedule deferred reset into bootloader DFU mode.
  // Return immediately so the CLI handler can send the reply first.
  // tick() will handle cleanup (stop tasks, radio off) and reset after the delay.
  ota_dfu_reset_at = millis() + 3000;  // 3s delay to ensure reply is transmitted

  return true;
}

/// @brief Collects board telemetry and appends to CayenneLPP packet
/// @param telemetry CayenneLPP packet to append data to
/// @return true if successful, false if telemetry data unavailable
bool InheroMr2Board::queryBoardTelemetry(CayenneLPP& telemetry) {
  const Telemetry* telemetryData = boardConfig.getTelemetryData();
  if (!telemetryData) {
    return false;
  }

  uint8_t batteryChannel = this->findNextFreeChannel(telemetry);
  uint8_t solarChannel = batteryChannel + 1;

  const BatterySOCStats* socStats = boardConfig.getSOCStats();
  bool hasValidSoc = (socStats && socStats->soc_valid);
  float socPercent = boardConfig.getStateOfCharge();
  // Requested precision: one decimal place
  socPercent = roundf(socPercent * 10.0f) / 10.0f;

  uint16_t ttlHours = boardConfig.getTTL_Hours();
  bool isInfiniteTtl = (socStats && socStats->soc_valid && !socStats->living_on_battery);
  constexpr float MAX_TTL_DAYS = 990.0f; // Max encodable LPP distance value

  // Battery channel
  // Field order:
  // 1) VBAT[V], 2) SOC[%] (optional), 3) IBAT[A], 4) TBAT[°C], 5) TTL[d] (optional)
  telemetry.addVoltage(batteryChannel, telemetryData->batterie.voltage / 1000.0f);
  if (hasValidSoc) {
    telemetry.addPercentage(batteryChannel, socPercent);
  }
  telemetry.addCurrent(batteryChannel, telemetryData->batterie.current / 1000.0f);
  if (telemetryData->batterie.temperature > -100.0f) {
    telemetry.addTemperature(batteryChannel, telemetryData->batterie.temperature);
  }

  // TTL handling:
  // - ttlHours > 0: send finite TTL in days
  // - surplus (infinite TTL): send max value
  // - unknown TTL (ttlHours == 0 and not surplus): send nothing
  if (ttlHours > 0) {
    telemetry.addDistance(batteryChannel, ttlHours / 24.0f);
  } else if (isInfiniteTtl) {
    telemetry.addDistance(batteryChannel, MAX_TTL_DAYS);
  }

  // Solar channel
  // Field order:
  // 1) VSOL[V], 2) ISOL[A], 3) MPPT_7D[%]
  telemetry.addVoltage(solarChannel, telemetryData->solar.voltage / 1000.0f);
  telemetry.addCurrent(solarChannel, telemetryData->solar.current / 1000.0f);
  telemetry.addPercentage(solarChannel, boardConfig.getMpptEnabledPercentage7Day());

  return true;
}

/// @brief Handles custom CLI getter commands for board configuration
/// @param getCommand Command string (without "board." prefix)
/// @param reply Buffer to write response to
/// @param maxlen Maximum length of reply buffer
/// @return true if command was handled, false otherwise
bool InheroMr2Board::getCustomGetter(const char* getCommand, char* reply, uint32_t maxlen) {

  // Trim trailing whitespace from command
  char trimmedCommand[100];
  strncpy(trimmedCommand, getCommand, sizeof(trimmedCommand) - 1);
  trimmedCommand[sizeof(trimmedCommand) - 1] = '\0';
  char* cmd = BoardConfigContainer::trim(trimmedCommand);

  if (strcmp(cmd, "bat") == 0) {
    snprintf(reply, maxlen, "%s",
             BoardConfigContainer::getBatteryTypeCommandString(boardConfig.getBatteryType()));
    return true;
  } else if (strcmp(cmd, "fmax") == 0) {
    const auto* props = BoardConfigContainer::getBatteryProperties(boardConfig.getBatteryType());
    if (props && props->ts_ignore) {
      snprintf(reply, maxlen, "N/A");
    } else {
      snprintf(
          reply, maxlen, "%s",
          BoardConfigContainer::getFrostChargeBehaviourCommandString(boardConfig.getFrostChargeBehaviour()));
    }
    return true;
  } else if (strcmp(cmd, "imax") == 0) {
    snprintf(reply, maxlen, "%s", boardConfig.getChargeCurrentAsStr());
    return true;
  } else if (strcmp(cmd, "mppt") == 0) {
    snprintf(reply, maxlen, "MPPT=%s", boardConfig.getMPPTEnabled() ? "1" : "0");
    return true;
  } else if (strcmp(cmd, "stats") == 0) {
    // Combined energy statistics: balance + MPPT
    const BatterySOCStats* socStats = boardConfig.getSOCStats();
    if (!socStats) {
      float mppt_pct = boardConfig.getMpptEnabledPercentage7Day();
      snprintf(reply, maxlen, "N/A M:%.0f%%", mppt_pct);
      return true;
    }

    // Balance info (mAh) - rolling windows (no midnight reset)
    // Include current-hour accumulators so data is visible before first hour boundary
    float last_24h_net = socStats->last_24h_net_mah
                       + socStats->current_hour_solar_mah
                       - socStats->current_hour_discharged_mah;
    float last_24h_charged = socStats->last_24h_charged_mah
                           + socStats->current_hour_charged_mah;
    float last_24h_discharged = socStats->last_24h_discharged_mah
                              + socStats->current_hour_discharged_mah;
    const char* status = socStats->living_on_battery ? "BAT" : "SOL";
    float avg3d = socStats->avg_3day_daily_net_mah;
    float avg3d_charged = socStats->avg_3day_daily_charged_mah;
    float avg3d_discharged = socStats->avg_3day_daily_discharged_mah;
    float avg7d = socStats->avg_7day_daily_net_mah;
    float avg7d_charged = socStats->avg_7day_daily_charged_mah;
    float avg7d_discharged = socStats->avg_7day_daily_discharged_mah;
    uint16_t ttl = boardConfig.getTTL_Hours();

    // MPPT info
    float mppt_pct = boardConfig.getMpptEnabledPercentage7Day();

    // Compact format: 24h/3d/7d Status MPPT% TTL
    char ttlBuf[16];
    if (ttl >= 24) {
      snprintf(ttlBuf, sizeof(ttlBuf), "%dd%dh", ttl / 24, ttl % 24);
    } else if (ttl > 0) {
      snprintf(ttlBuf, sizeof(ttlBuf), "%dh", ttl);
    } else {
      snprintf(ttlBuf, sizeof(ttlBuf), "N/A");
    }
    snprintf(reply, maxlen, "%+.0f/%+.0f/%+.0fmAh C:%.0f D:%.0f 3C:%.0f 3D:%.0f 7C:%.0f 7D:%.0f %s M:%.0f%% T:%s",
             last_24h_net, avg3d, avg7d, last_24h_charged, last_24h_discharged, avg3d_charged, avg3d_discharged,
             avg7d_charged, avg7d_discharged, status, mppt_pct, ttlBuf);
    return true;
  } else if (strcmp(cmd, "cinfo") == 0) {
    char infoBuffer[100];
    boardConfig.getChargerInfo(infoBuffer, sizeof(infoBuffer));
    snprintf(reply, maxlen, "%s", infoBuffer);
    return true;
  } else if (strcmp(cmd, "bqdiag") == 0) {
    char diagBuffer[100];
    boardConfig.getBqDiagnostics(diagBuffer, sizeof(diagBuffer));
    snprintf(reply, maxlen, "%s", diagBuffer);
    return true;
  } else if (strcmp(cmd, "selftest") == 0) {
    char stBuffer[64];
    boardConfig.getSelfTest(stBuffer, sizeof(stBuffer));
    snprintf(reply, maxlen, "%s", stBuffer);
    return true;
  } else if (strcmp(cmd, "socdebug") == 0) {
    Ina228Driver* ina = boardConfig.getIna228Driver();
    if (!ina) {
      snprintf(reply, maxlen, "INA228 n/a");
      return true;
    }
    const BatterySOCStats* s = boardConfig.getSOCStats();
    uint16_t scal = ina->readShuntCalRegister();
    float chg = ina->readCharge_mAh();
    float cur = ina->readCurrent_mA_precise();
    uint32_t rtc = boardConfig.getRTCTimestamp();
    snprintf(reply, maxlen,
             "S=%u I=%.1f C=%.1f hC%.1f hD%.1f n=%u t=%lu d=%.2f",
             scal, cur, chg,
             s->current_hour_charged_mah,
             s->current_hour_discharged_mah,
             s->soc_update_count,
             (unsigned long)rtc,
             s->temp_derating_factor);
    return true;
  } else if (strcmp(cmd, "telem") == 0) {
    const Telemetry* telemetry = boardConfig.getTelemetryData();
    if (!telemetry) {
      snprintf(reply, maxlen, "Err: Telemetry unavailable");
      return true;
    }

    // Use precise battery current from telemetry (INA228)
    float precise_current_ma = telemetry->batterie.current;

    // Get SOC info
    float soc = boardConfig.getStateOfCharge();
    const BatterySOCStats* socStats = boardConfig.getSOCStats();

    // Format currents: battery current with 1 decimal, solar current without decimals
    // INA228 driver returns correctly signed values: positive=charging, negative=discharging
    char bat_current_str[16];
    snprintf(bat_current_str, sizeof(bat_current_str), "%.1fmA", precise_current_ma);

    char sol_current_str[16];
    int16_t sol_current = telemetry->solar.current;
    if (sol_current == 0) {
      snprintf(sol_current_str, sizeof(sol_current_str), "0mA");
    } else if (sol_current < 50) {
      snprintf(sol_current_str, sizeof(sol_current_str), "<50mA");
    } else if (sol_current <= 100) {
      snprintf(sol_current_str, sizeof(sol_current_str), "~%dmA", (int)sol_current);
    } else {
      snprintf(sol_current_str, sizeof(sol_current_str), "%dmA", (int)sol_current);
    }

    // Format temperature: "N/A" when NTC unavailable (no solar)
    char temp_str[8];
    if (telemetry->batterie.temperature <= -100.0f) {
      snprintf(temp_str, sizeof(temp_str), "N/A");
    } else {
      snprintf(temp_str, sizeof(temp_str), "%.0fC", telemetry->batterie.temperature);
    }

    if (socStats && socStats->soc_valid) {
      // Trapped Charge model: cold locks the bottom of the discharge curve.
      // trapped% = (1 − f(T)) × 100, extractable% = max(0, SOC% − trapped%)
      if (socStats->temp_derating_factor < 0.999f && socStats->temp_derating_factor > 0.0f) {
        float trapped_pct = (1.0f - socStats->temp_derating_factor) * 100.0f;
        float derated_soc = soc - trapped_pct;
        if (derated_soc < 0.0f) derated_soc = 0.0f;
        if (derated_soc > 100.0f) derated_soc = 100.0f;
        snprintf(reply, maxlen, "B:%.2fV/%s/%s SOC:%.1f%% (%.0f%%) S:%.2fV/%s", telemetry->batterie.voltage / 1000.0f,
                 bat_current_str, temp_str, soc, derated_soc, telemetry->solar.voltage / 1000.0f,
                 sol_current_str);
      } else {
        snprintf(reply, maxlen, "B:%.2fV/%s/%s SOC:%.1f%% S:%.2fV/%s", telemetry->batterie.voltage / 1000.0f,
                 bat_current_str, temp_str, soc, telemetry->solar.voltage / 1000.0f,
                 sol_current_str);
      }
    } else {
      snprintf(reply, maxlen, "B:%.2fV/%s/%s SOC:N/A S:%.2fV/%s", telemetry->batterie.voltage / 1000.0f,
               bat_current_str, temp_str, telemetry->solar.voltage / 1000.0f,
               sol_current_str);
    }
    return true;
  } else if (strcmp(cmd, "conf") == 0) {
    // Display all configuration values
    const char* batType = BoardConfigContainer::getBatteryTypeCommandString(boardConfig.getBatteryType());
    const char* frostBehaviour;
    const auto* confProps = BoardConfigContainer::getBatteryProperties(boardConfig.getBatteryType());
    if (confProps && confProps->ts_ignore) {
      frostBehaviour = "N/A";
    } else {
      frostBehaviour =
          BoardConfigContainer::getFrostChargeBehaviourCommandString(boardConfig.getFrostChargeBehaviour());
    }
    if (boardConfig.getBatteryType() == BoardConfigContainer::BAT_UNKNOWN) {
      snprintf(reply, maxlen, "B:%s (no battery, charging disabled)", batType);
    } else {
      float chargeVoltage = boardConfig.getMaxChargeVoltage();
      float voltage0Soc = getLowVoltageWakeThreshold() / 1000.0f;
      const char* imax = boardConfig.getChargeCurrentAsStr();
      bool mpptEnabled = boardConfig.getMPPTEnabled();

      snprintf(reply, maxlen, "B:%s F:%s M:%s I:%s Vco:%.2f V0:%.2f", batType, frostBehaviour,
               mpptEnabled ? "1" : "0", imax, chargeVoltage, voltage0Soc);
    }
    return true;
  } else if (strcmp(cmd, "tccal") == 0) {
    // Get current NTC temperature calibration offset
    float offset = boardConfig.getTcCalOffset();
    snprintf(reply, maxlen, "TC offset: %+.2f C (0.00=default)", offset);
    return true;
  } else if (strcmp(cmd, "leds") == 0) {
    // Get LED enable state (heartbeat + BQ stat LED)
    bool enabled = boardConfig.getLEDsEnabled();
    snprintf(reply, maxlen, "LEDs: %s (Heartbeat + BQ Stat)", enabled ? "ON" : "OFF");
    return true;
  } else if (strcmp(cmd, "batcap") == 0) {
    // Get battery capacity in mAh - show if default or explicitly set
    float capacity_mah = boardConfig.getBatteryCapacity();
    bool explicitly_set = boardConfig.isBatteryCapacitySet();
    if (explicitly_set) {
      snprintf(reply, maxlen, "%.0f mAh (set)", capacity_mah);
    } else {
      snprintf(reply, maxlen, "%.0f mAh (default)", capacity_mah);
    }
    return true;
  }

  snprintf(reply, maxlen,
           "Err: bat|fmax|imax|mppt|telem|stats|cinfo|conf|tccal|leds|batcap");
  return true;
}

/// @brief Handles custom CLI setter commands for board configuration
/// @param setCommand Command string with value (without "board." prefix)
/// @return Status message ("OK" on success, error message on failure)
const char* InheroMr2Board::setCustomSetter(const char* setCommand) {

  static char ret[100];
  memset(ret, 0, sizeof(ret)); // Clear buffer to prevent garbage data

  if (strncmp(setCommand, "bat ", 4) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[4]));
    BoardConfigContainer::BatteryType bt = BoardConfigContainer::getBatteryTypeFromCommandString(value);
    if (bt != BoardConfigContainer::BatteryType::BAT_UNKNOWN || strcmp(value, "none") == 0) {
      boardConfig.setBatteryType(bt);
      snprintf(ret, sizeof(ret), "Bat set to %s",
               BoardConfigContainer::getBatteryTypeCommandString(boardConfig.getBatteryType()));
      return ret;
    } else {
      snprintf(ret, sizeof(ret), "Err: Try one of: %s", BoardConfigContainer::getAvailableBatOptions());
      return ret;
    }
  } else if (strncmp(setCommand, "fmax ", 5) == 0) {
    const auto* fmaxProps = BoardConfigContainer::getBatteryProperties(boardConfig.getBatteryType());
    if (fmaxProps && fmaxProps->ts_ignore) {
      snprintf(ret, sizeof(ret), "Err: Fmax setting N/A for this chemistry (JEITA disabled)");
      return ret;
    }

    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[5]));
    BoardConfigContainer::FrostChargeBehaviour fcb =
        BoardConfigContainer::getFrostChargeBehaviourFromCommandString(value);
    if (fcb != BoardConfigContainer::FrostChargeBehaviour::REDUCE_UNKNOWN) {
      boardConfig.setFrostChargeBehaviour(fcb);
      snprintf(
          ret, sizeof(ret), "Fmax charge current set to %s of imax",
          BoardConfigContainer::getFrostChargeBehaviourCommandString(boardConfig.getFrostChargeBehaviour()));
      return ret;
    } else {
      snprintf(ret, sizeof(ret), "Err: Try one of: %s",
               BoardConfigContainer::getAvailableFrostChargeBehaviourOptions());
      return ret;
    }
  } else if (strncmp(setCommand, "imax ", 5) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[5]));
    int ma = atoi(value);
    if (ma >= 50 && ma <= 1500) {
      boardConfig.setMaxChargeCurrent_mA(ma);
      snprintf(ret, sizeof(ret), "Max charge current set to %s", boardConfig.getChargeCurrentAsStr());
      return ret;
    } else {
      return "Err: Try 50-1500";
    }
  } else if (strncmp(setCommand, "mppt ", 5) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[5]));
    // Convert to lowercase for case-insensitive comparison
    char lowerValue[20];
    strncpy(lowerValue, value, sizeof(lowerValue) - 1);
    lowerValue[sizeof(lowerValue) - 1] = '\0';
    for (char* p = lowerValue; *p; ++p)
      *p = tolower(*p);

    if (strcmp(lowerValue, "true") == 0 || strcmp(lowerValue, "1") == 0) {
      boardConfig.setMPPTEnable(true);
      snprintf(ret, sizeof(ret), "MPPT enabled");
      return ret;
    } else if (strcmp(lowerValue, "false") == 0 || strcmp(lowerValue, "0") == 0) {
      boardConfig.setMPPTEnable(false);
      snprintf(ret, sizeof(ret), "MPPT disabled");
      return ret;
    } else {
      return "Err: Try true|false or 1|0";
    }
  } else if (strncmp(setCommand, "batcap ", 7) == 0) {
    // Set battery capacity
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[7]));
    float capacity_mah = atof(value);

    if (boardConfig.setBatteryCapacity(capacity_mah)) {
      snprintf(ret, sizeof(ret), "Battery capacity set to %.0f mAh", capacity_mah);
    } else {
      snprintf(ret, sizeof(ret), "Err: Invalid capacity (100-100000 mAh)");
    }
    return ret;
  } else if (strncmp(setCommand, "tccal", 5) == 0) {
    // NTC temperature calibration:
    //   set board.tccal          → auto-read BME280 as reference
    //   set board.tccal reset     → reset to 0.00
    const char* rest = &setCommand[5];

    // Skip optional space
    if (*rest == ' ') rest++;

    const char* value = BoardConfigContainer::trim(const_cast<char*>(rest));

    // Check for reset command
    if (strcmp(value, "reset") == 0 || strcmp(value, "RESET") == 0) {
      if (boardConfig.setTcCalOffset(0.0f)) {
        snprintf(ret, sizeof(ret), "TC calibration reset to 0.00 (default)");
      } else {
        snprintf(ret, sizeof(ret), "Err: Failed to reset TC calibration");
      }
      return ret;
    }

    // Auto-read BME280 as reference (averages 5 samples each)
    float bme_avg = 0.0f;
    float new_offset = boardConfig.performTcCalibration(&bme_avg);
    if (new_offset > -900.0f) {
      snprintf(ret, sizeof(ret), "TC auto-cal: BME=%.1f offset=%+.2f C", bme_avg, new_offset);
    } else {
      snprintf(ret, sizeof(ret), "Err: Auto-cal failed (BME280/NTC error?)");
    }
    return ret;
  } else if (strncmp(setCommand, "leds ", 5) == 0) {
    // Enable/disable heartbeat LED and BQ stat LED
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[5]));
    bool enabled = (strcmp(value, "1") == 0 || strcmp(value, "on") == 0 || strcmp(value, "ON") == 0);
    bool disabled = (strcmp(value, "0") == 0 || strcmp(value, "off") == 0 || strcmp(value, "OFF") == 0);

    if (enabled || disabled) {
      boardConfig.setLEDsEnabled(enabled);
      snprintf(ret, sizeof(ret), "LEDs %s (Heartbeat + BQ Stat)", enabled ? "enabled" : "disabled");
      return ret;
    } else {
      snprintf(ret, sizeof(ret), "Err: Use 'on/1' or 'off/0'");
      return ret;
    }
  } else if (strncmp(setCommand, "soc ", 4) == 0) {
    // Manually set SOC percentage (e.g. after reboot with known SOC)
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[4]));
    float soc_percent = atof(value);

    if (BoardConfigContainer::setSOCManually(soc_percent)) {
      snprintf(ret, sizeof(ret), "SOC set to %.1f%%", soc_percent);
    } else {
      snprintf(ret, sizeof(ret), "Err: Invalid SOC (0-100) or INA228 not ready");
    }
    return ret;
  }

  snprintf(ret, sizeof(ret), "Err: bat|imax|fmax|mppt|batcap|tccal|leds|soc");
  return ret;
}

// ===== Power Management Methods (Rev 1.1) =====

/// @brief Get low-voltage sleep threshold (chemistry-specific)
/// @return Sleep threshold in millivolts (INA228 ALERT fires here)
uint16_t InheroMr2Board::getLowVoltageSleepThreshold() {
  BoardConfigContainer::BatteryType chemType = boardConfig.getBatteryType();
  return BoardConfigContainer::getLowVoltageSleepThreshold(chemType);
}

/// @brief Get low-voltage wake threshold (chemistry-specific)
/// @return Wake threshold in millivolts (0% SOC marker, RTC wake decision)
uint16_t InheroMr2Board::getLowVoltageWakeThreshold() {
  BoardConfigContainer::BatteryType chemType = boardConfig.getBatteryType();
  return BoardConfigContainer::getLowVoltageWakeThreshold(chemType);
}

/// @brief Put SX1262 radio and SPI pins into lowest power state for System Sleep.
/// Must be called before ANY sd_power_system_off() call — both from initiateShutdown()
/// and Early Boot quick-shutdown paths.
///
/// @param radioInitialized true if SPI + radio have been initialized (normal shutdown),
///                         false if called from Early Boot before SPI.begin() / radio.begin().
///
/// CRITICAL: Do NOT call SPI.end() here! SPI.end() runs nrf_gpio_cfg_default() which
/// briefly puts NSS/SCK into "disconnected input" (floating). The SX1262's internal 48kΩ
/// pull-up isn't fast enough — floating SCK triggers a wake from Cold Sleep to Standby RC
/// (~600µA). Once awake, only an explicit SPI SetSleep command can put it back — but we've
/// already released SPI.
///
/// Instead, pre-configure the GPIO PORT registers (OUTSET/PIN_CNF) while SPIM is still
/// active. These are "shadow" values that take effect when SPIM is disabled. System Sleep
/// stops all peripherals including SPIM, and the GPIO PORT seamlessly takes over — no
/// glitch, no floating pins, SX1262 stays in Cold Sleep (~160nA).
void InheroMr2Board::prepareRadioForSystemOff(bool radioInitialized) {
  if (radioInitialized) {
    // Put SX1262 into Cold Sleep via SPI while SPIM is still active
    radio_driver.powerOff();  // calls radio.sleep(false) — cold sleep, lowest power
    delay(10);
  } else {
    // Early Boot: SPI/RadioLib not initialized. The SX1262 may be in:
    //   a) POR Standby RC (~600µA) — first power-on or VDD glitch
    //   b) Cold Sleep (~160nA) — if previous shutdown latched pins correctly
    // Case (b) is fine, case (a) wastes ~600µA. We can't tell which, so always
    // send SetSleep to ensure Cold Sleep.
    //
    // Use bit-banged SPI — completely independent of Arduino SPIClass and SPIM
    // peripheral configuration. variant.h defines SPI pins as WB header pins
    // (P0.03/P0.29/P0.30) which are NOT the LoRa radio pins (P1.11/P1.12/P1.13).
    // Bit-bang directly on P_LORA_* pins avoids all BSP/SPIM conflicts.

    // Configure SPI GPIOs
    pinMode(P_LORA_NSS, OUTPUT);
    digitalWrite(P_LORA_NSS, HIGH);
    pinMode(P_LORA_SCLK, OUTPUT);
    digitalWrite(P_LORA_SCLK, LOW);   // CPOL=0: idle LOW
    pinMode(P_LORA_MOSI, OUTPUT);
    digitalWrite(P_LORA_MOSI, LOW);
    pinMode(P_LORA_BUSY, INPUT);

    // SX1262 Wake-Up + SetSleep with correct NSS double-cycle.
    //
    // SX1262 Datasheet §13.1.1: "When exiting sleep mode using the falling
    // edge of NSS pin, the SPI command is NOT interpreted by the transceiver
    // so a SUBSEQUENT falling edge of NSS is therefore necessary."
    //
    // Phase 1: Wake-up cycle (NSS LOW → wait BUSY LOW → NSS HIGH)
    // If SX1262 is in Cold Sleep: NSS LOW wakes it, BUSY goes HIGH (~3.5ms),
    //   then LOW when ready. We wait for BUSY LOW.
    // If SX1262 is in Standby RC: NSS LOW does nothing, BUSY already LOW.
    // Either way, after this phase SX1262 is in Standby RC and ready.
    digitalWrite(P_LORA_NSS, LOW);
    delayMicroseconds(2);
    // Wait for BUSY LOW — SX1262 ready after wake-up or already awake
    uint32_t t0 = millis();
    while (digitalRead(P_LORA_BUSY) == HIGH && (millis() - t0) < 10) {
      delayMicroseconds(100);
    }
    digitalWrite(P_LORA_NSS, HIGH);  // End wake-up cycle
    delayMicroseconds(10);           // Minimum NSS HIGH time

    // Phase 2: Actual SetSleep command on fresh NSS falling edge
    // Opcode 0x84, config 0x00 = Cold Start (no config retention, TCXO off)
    // SPI Mode 0 (CPOL=0, CPHA=0): data sampled on rising edge, MSB first
    static const uint8_t cmd[2] = { 0x84, 0x00 };

    digitalWrite(P_LORA_NSS, LOW);
    delayMicroseconds(2);  // NSS setup time

    for (int b = 0; b < 2; b++) {
      uint8_t byte = cmd[b];
      for (int i = 7; i >= 0; i--) {
        digitalWrite(P_LORA_MOSI, (byte >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(P_LORA_SCLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(P_LORA_SCLK, LOW);
        delayMicroseconds(1);
      }
    }

    digitalWrite(P_LORA_MOSI, LOW);
    delayMicroseconds(1);
    digitalWrite(P_LORA_NSS, HIGH);

    delay(1);  // Allow SX1262 to enter Cold Sleep (~500ns typ)
  }

  // Power off PE4259 RF switch (cut VDD to antenna switch)
  digitalWrite(SX126X_POWER_EN, LOW);

  // Latch ALL SX1262 SPI input pins to defined states during System OFF.
  // If SCLK/MOSI float (INPUT_DISCONNECT → ~VDD/2), the SX1262 CMOS input
  // buffers draw shoot-through current (hundreds of µA).
  // NSS=HIGH (keep Cold Sleep), SCLK=LOW (CPOL=0 idle), MOSI=LOW (idle).
  uint32_t pin_cfg_out = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
                          (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
                          (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
                          (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
                          (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
  NRF_P1->OUTSET = (1UL << 10);                         // NSS  (P1.10) = HIGH
  NRF_P1->OUTCLR = (1UL << 11) | (1UL << 12);          // SCLK (P1.11) = LOW, MOSI (P1.12) = LOW
  NRF_P1->PIN_CNF[10] = pin_cfg_out;  // NSS
  NRF_P1->PIN_CNF[11] = pin_cfg_out;  // SCLK
  NRF_P1->PIN_CNF[12] = pin_cfg_out;  // MOSI
}

/// @brief Set ALL GPIO pins to INPUT_DISCONNECT except the 3 pins needed during System Sleep.
/// Must be called AFTER Wire.end(), AFTER prepareRadioForSystemOff(), and AFTER all I2C/SPI.
///
/// Comprehensive approach: Instead of chasing individual leaky pins, reset ALL pins to the
/// power-on-reset default (Input, Disconnected, No pull). This eliminates ALL possible
/// current leakage through GPIO — pull-ups driving OD devices LOW, OUTPUT pins driving
/// LEDs/loads, etc.
///
/// CRITICAL: Adafruit BSP init() runs NRF_P0->OUTSET = NRF_P1->OUTSET = 0xFFFFFFFF on
/// every boot BEFORE our code. Any pin left as OUTPUT from the previous System Sleep cycle
/// gets its output latch set HIGH. For LEDs (P1.03, P1.04) this means LEDs turn ON.
/// For CE (P0.04) this means charge enable pulse. By setting unused pins to INPUT_DISCONNECT,
/// DIR=Input prevents the BSP OUTSET from causing physical pin changes on the next boot.
///
/// Only 2 pins are preserved:
///   P0.04  (BQ_CE_PIN)   — OUTPUT HIGH: solar charging active during sleep
///   P0.17  (RTC_INT_PIN) — INPUT_PULLUP + SENSE_Low: System Sleep wake source
/// NSS (P1.10) has internal pull-up inside RAK4630 — no external latch needed.
void InheroMr2Board::disconnectLeakyPullups() {
  uint32_t pin_cfg_discon = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                            (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
                            (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
                            (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
                            (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);

  // P0: pins 0–31, skip P0.04 (CE) and P0.17 (RTC INT)
  for (uint8_t pin = 0; pin < 32; pin++) {
    if (pin == 4 || pin == 17) continue;  // BQ_CE_PIN, RTC_INT_PIN
    NRF_P0->PIN_CNF[pin] = pin_cfg_discon;
  }

  // P1: pins 0–15, skip SX1262 SPI pins latched by prepareRadioForSystemOff()
  // NSS (P1.10)=HIGH, SCLK (P1.11)=LOW, MOSI (P1.12)=LOW
  for (uint8_t pin = 0; pin < 16; pin++) {
    if (pin == 10 || pin == 11 || pin == 12) continue;  // NSS, SCLK, MOSI
    NRF_P1->PIN_CNF[pin] = pin_cfg_discon;
  }
}

/// @brief Initiate controlled shutdown with filesystem protection (Rev 1.1)
/// @param reason Shutdown reason code (stored in GPREGRET2 for next boot)
///
/// Rev 1.1 low-voltage shutdown uses System Sleep with GPIO latch (< 500µA total):
/// - INA228 enters shutdown mode (~3.5µA)
/// - BQ CE pin latched HIGH via FET (solar charging continues autonomously)
/// - RTC countdown timer configured for periodic wake
/// - nRF52 enters SYSTEMOFF (~1.5µA) — GPIO latches preserved
/// - RTC wake triggers reboot; Early Boot checks voltage for boot vs sleep-again
void InheroMr2Board::initiateShutdown(uint8_t reason) {
  MESH_DEBUG_PRINTLN("PWRMGT: Initiating shutdown (reason=0x%02X)", reason);

  // 1. Stop background tasks to prevent filesystem corruption
  BoardConfigContainer::stopBackgroundTasks();

  // 2. INA228: Shutdown mode to minimize sleep current (~3.5µA vs ~300µA continuous)
  //    No BUVL monitoring needed in sleep — RTC wakes us for voltage check.
  Ina228Driver* ina = boardConfig.getIna228Driver();
  if (ina) {
    // Release INA228 ALERT pin before shutdown.
    // The under-voltage alert that triggered sleep is latched (ALATCH=1) — ALERT
    // stays LOW. RAK4630 has internal pull-up on P1.02 → 3.3V through pull-up
    // into OD transistor = ~330µA wasted in System Sleep.
    // Fix: 1) Disable ALATCH by writing DIAG_ALRT=0 (transparent mode + clear flags)
    //      2) Set BUVL=0 (no threshold → no alert condition in transparent mode)
    //      3) Then shutdown ADC
    ina->enableAlert(false, false, false);  // DIAG_ALRT=0: ALATCH=0, clear all flags
    ina->setUnderVoltageAlert(0);           // BUVL=0: disable under-voltage comparison
    ina->shutdown();
  }

  // 3. SX1262 sleep + SPI cleanup (prevents ~4mA leakage in System Sleep)
  prepareRadioForSystemOff();

  // 4. LEDs off before sleep
  digitalWrite(PIN_LED1, LOW);
  digitalWrite(PIN_LED2, LOW);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE) {
    MESH_DEBUG_PRINTLN("PWRMGT: Low voltage shutdown - entering System Sleep with CE latched");

    delay(100); // Allow I/O to complete

    // 5. Latch BQ CE pin HIGH (FET ON = CE LOW = charge enabled)
    // GPIO output latch survives System Sleep as long as VDD is present
#ifdef BQ_CE_PIN
    digitalWrite(BQ_CE_PIN, HIGH);
    MESH_DEBUG_PRINTLN("PWRMGT: CE latched HIGH (solar charging active in sleep)");
#endif

    // 5b. BQ25798 — Disable ADC (saves ~500µA continuous draw)
    // Must be AFTER CE=HIGH — charge enable may re-enable ADC internally.
    Wire.beginTransmission(BQ25798_I2C_ADDR);
    Wire.write(0x2E);  // ADC_CONTROL register
    Wire.write(0x00);  // ADC_EN=0, ADC disabled
    Wire.endTransmission();

    // 5c. BQ25798 — Mask all interrupts and clear flags to de-assert INT pin.
    // Default masks are 0x00 (all unmasked). Any pending flag holds INT LOW,
    // and INPUT_PULLUP on BQ_INT_PIN wastes ~254µA through the pull-up.
    { // Mask all interrupts
      const uint8_t mask_regs[] = {0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D};
      for (uint8_t r : mask_regs) {
        Wire.beginTransmission(BQ25798_I2C_ADDR);
        Wire.write(r);
        Wire.write(0xFF);
        Wire.endTransmission();
      }
      // Read-to-clear all flag registers
      const uint8_t flag_regs[] = {0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
      for (uint8_t r : flag_regs) {
        Wire.beginTransmission(BQ25798_I2C_ADDR);
        Wire.write(r);
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)BQ25798_I2C_ADDR, (uint8_t)1);
        while (Wire.available()) Wire.read();
      }
    }

    // 5d. BME280 @ 0x76 — Force Sleep mode (saves ~1-7µA)
    // After normal operation readBmeTemperature() may have left BME280 in NORMAL mode.
    // Harmless NACK if no BME280 populated.
    Wire.beginTransmission(0x76);
    Wire.write(0xF4);  // ctrl_meas register
    Wire.write(0x00);  // MODE=00 (Sleep), all oversampling off
    Wire.endTransmission();
    MESH_DEBUG_PRINTLN("PWRMGT: BQ25798 ADC/INT + BME280 shut down");

    // 6. Configure RTC to wake us up periodically for voltage check
    configureRTCWake(LOW_VOLTAGE_SLEEP_MINUTES);

    // 7. Clear GPIO LATCH for RTC INT pin.
    // If a previous RTC wake cycle set the LATCH (retained across System Sleep),
    // DETECT would fire immediately → instant wake → boot loop.
    NRF_P0->LATCH = (1UL << RTC_INT_PIN);

    // 8. Release I2C buses (done AFTER RTC config, which uses Wire)
    Wire.end();

    // 9. Disconnect all GPIO pull-ups on OD/I2C pins to prevent leakage
    disconnectLeakyPullups();

    // 10. Store shutdown reason for Early Boot decision
    NRF_POWER->GPREGRET2 = GPREGRET2_LOW_VOLTAGE_SLEEP | reason;

    MESH_DEBUG_PRINTLN("PWRMGT: Entering System Sleep (< 500uA)");
    delay(50);

    sd_power_system_off();
    // Fallback if SoftDevice not enabled
    NRF_POWER->SYSTEMOFF = 1;
    while (1) __WFE();
  }

  // Non-low-voltage shutdown (user request, thermal): use System OFF
  Wire.end();
  disconnectLeakyPullups();
  NRF_POWER->GPREGRET2 = reason;

  MESH_DEBUG_PRINTLN("PWRMGT: Entering SYSTEMOFF");
  delay(50);

  // Clear LATCH to prevent spurious wake
  NRF_P0->LATCH = (1UL << RTC_INT_PIN);

  sd_power_system_off();
  // Fallback if SoftDevice not enabled
  NRF_POWER->SYSTEMOFF = 1;
  while (1) __WFE();
}

/// @brief Configure RV-3028 RTC countdown timer for periodic wake-up
/// @param minutes Wake-up interval in minutes (1-4095, RV-3028 12-bit limit)
void InheroMr2Board::configureRTCWake(uint32_t minutes) {
#if defined(INHERO_MR2)
  rtc_clock.setLocked(true);
#endif
  uint16_t countdown_ticks = static_cast<uint16_t>(minutes == 0 ? LOW_VOLTAGE_SLEEP_MINUTES : minutes);
  if (countdown_ticks == 0) {
    countdown_ticks = 1;
  }
  // RV-3028 Timer Value register is 12-bit (max 4095)
  if (countdown_ticks > 4095) {
    countdown_ticks = 4095;
  }
  MESH_DEBUG_PRINTLN("PWRMGT: Configuring RTC wake in %u minutes",
                     static_cast<unsigned>(countdown_ticks));

  // === RTC Timer Configuration per Manual Section 4.8.2 ===
  // Step 1: Stop Timer and clear flags
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_CTRL1);
  Wire.write(0x00); // TE=0, TD=00 (stop timer)
  Wire.endTransmission();

  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_CTRL2);
  Wire.write(0x00); // TIE=0 (disable interrupt)
  Wire.endTransmission();

  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_STATUS);
  Wire.write(0x00); // Clear TF flag
  Wire.endTransmission();

  // Step 2: Set Timer Value (ticks at 1/60 Hz)
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_TIMER_VALUE_0);
  Wire.write(countdown_ticks & 0xFF);        // Lower 8 bits
  Wire.write((countdown_ticks >> 8) & 0x0F); // Upper 4 bits
  Wire.endTransmission();

  // Step 3: Configure Timer (1/60 Hz clock, Single shot mode)
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_CTRL1);
  Wire.write(0x07); // TE=1 (Enable), TD=11 (1/60 Hz), TRPT=0 (Single shot)
  Wire.endTransmission();

  // Step 4: Enable Timer Interrupt
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(RV3028_REG_CTRL2);
  Wire.write(0x10); // TIE=1 (Timer Interrupt Enable, bit 4)
  Wire.endTransmission();

  MESH_DEBUG_PRINTLN("PWRMGT: RTC countdown configured (%u ticks at 1/60 Hz)", countdown_ticks);

#if defined(INHERO_MR2)
  rtc_clock.setLocked(false);
#endif
}

/// @brief RTC interrupt handler - called when countdown timer expires
void InheroMr2Board::rtcInterruptHandler() {
  // RTC countdown elapsed - device woke from SYSTEMOFF
  // Defer I2C work to the main loop to avoid ISR I2C collisions.
  rtc_irq_pending = true;
}

// ===== Helper Functions =====

/// @brief Helper function to get LPP data length for a given type
/// @param type LPP data type
/// @return Data length in bytes, or 0 if unknown type
static uint8_t getLPPDataLength(uint8_t type) {
  switch (type) {
  case LPP_DIGITAL_INPUT:
  case LPP_DIGITAL_OUTPUT:
  case LPP_PRESENCE:
  case LPP_RELATIVE_HUMIDITY:
  case LPP_PERCENTAGE:
  case LPP_SWITCH:
    return 1;

  case LPP_ANALOG_INPUT:
  case LPP_ANALOG_OUTPUT:
  case LPP_LUMINOSITY:
  case LPP_TEMPERATURE:
  case LPP_BAROMETRIC_PRESSURE:
  case LPP_VOLTAGE:
  case LPP_CURRENT:
  case LPP_ALTITUDE:
  case LPP_POWER:
  case LPP_DIRECTION:
  case LPP_CONCENTRATION:
    return 2;

  case LPP_COLOUR:
    return 3;

  case LPP_GENERIC_SENSOR:
  case LPP_FREQUENCY:
  case LPP_DISTANCE:
  case LPP_ENERGY:
  case LPP_UNIXTIME:
    return 4;

  case LPP_ACCELEROMETER:
  case LPP_GYROMETER:
    return 6;

  case LPP_GPS:
    return 9;

  case LPP_POLYLINE:
    return 8; // minimum size

  default:
    return 0; // Unknown type
  }
}

/// @brief Find next available channel number in CayenneLPP packet
/// @param lpp CayenneLPP packet to analyze
/// @return Next free channel number (highest used channel + 1)
/// @note This method parses the LPP buffer to find the highest channel number in use,
///       then returns the next available channel. Used by queryBoardTelemetry() to
///       append board-specific telemetry without channel conflicts.
uint8_t InheroMr2Board::findNextFreeChannel(CayenneLPP& lpp) {
  uint8_t max_channel = 0;
  uint8_t cursor = 0;
  uint8_t* buffer = lpp.getBuffer();
  uint8_t size = lpp.getSize();

  while (cursor < size) {
    // Need at least 2 bytes: channel + type
    if (cursor + 1 >= size) break;

    uint8_t channel = buffer[cursor];
    uint8_t type = buffer[cursor + 1];
    uint8_t data_len = getLPPDataLength(type);

    // Unknown type - can't determine length, stop parsing
    if (data_len == 0) break;

    // Update max channel
    if (channel > max_channel) max_channel = channel;

    // Move to next entry
    cursor += 2 + data_len;
  }

  // Return next channel after the highest parsed one.
  // If packet is empty, start at channel 1.
  return max_channel + 1;
}
