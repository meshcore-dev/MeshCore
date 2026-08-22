/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * Inhero MR2 Board Implementation
 */

#include "InheroMr2Board.h"

#include "BoardConfigContainer.h"
#include "helpers/BatteryOcvMapping.h"
#include "helpers/BqLowPowerSetup.h"
#include "helpers/CliCommands.h"
#include "helpers/I2cBusRecovery.h"
#include "helpers/Rv3028Wake.h"
#include "helpers/SystemSleepGpio.h"
#include "helpers/UsbAutoManagement.h"
#include "target.h"

#include <Arduino.h>
#include <Wire.h>
#include <nrf_soc.h>

static BoardConfigContainer boardConfig;
volatile bool InheroMr2Board::rtc_irq_pending = false;
volatile uint32_t InheroMr2Board::ota_dfu_reset_at = 0;

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
    inhero::clearTimerFlag();

    // RTC INT: must have SENSE_Low for System Sleep wake-up
    NRF_GPIO->PIN_CNF[RTC_INT_PIN] =
        (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
        (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |
        (GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos) |
        (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
        (GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);

    uint16_t vbat_mv = Ina228Driver::readVBATDirect(&Wire, INA228_I2C_ADDR);
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

      // Put INA228 + BQ25798 into a state that draws minimal current during System Sleep.
      inhero::prepareIcsForSystemOff();

      // SX1262: Send SetSleep command AND latch NSS HIGH.
      // After System-ON reset, SX1262 may be in Standby RC (~600µA).
      // Both are needed: SetSleep puts it to Cold Sleep, NSS latch prevents re-wake.
      inhero::prepareRadioForSystemOff(false);

      configureRTCWake(LOW_VOLTAGE_SLEEP_MINUTES);
      NRF_P0->LATCH = (1UL << RTC_INT_PIN);
      Wire.end();

      // Disconnect GPIO pull-ups before System Sleep (Wire.end() keeps SDA/SCL
      // pull-ups active on nRF52 — each held-LOW line wastes ~250µA).
      inhero::disconnectLeakyPullups();

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
    MESH_DEBUG_PRINTLN("LV-Wake: Voltage recovered (%dmV >= %dmV) - normal boot", vbat_mv, wake_threshold);
  }

  // === Standard boot path (ColdBoot, recovery, or non-LV wake) ===
  bool isLowVoltageRecovery = ((shutdown_reason & 0x03) == SHUTDOWN_REASON_LOW_VOLTAGE);

  pinMode(PIN_VBAT_READ, INPUT);

  // BQ25798 CE: drive LOW on boot so the external FET stays OFF (Rev 1.1 inverts
  // logic via DMN2004TK-7). configureChemistry() raises it after successful I2C init.
#ifdef BQ_CE_PIN
  pinMode(BQ_CE_PIN, OUTPUT);
  digitalWrite(BQ_CE_PIN, LOW);
#endif

  // PE4259 RF switch VDD (P1.05 → PE4259 pin 6). Required for TX/RX; DIO2 drives CTRL.
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
  inhero::recoverI2cBus(PIN_BOARD_SDA, PIN_BOARD_SCL);

  Wire.begin();
  delay(50); // Give I2C bus time to stabilize

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
    uint16_t vbat_mv = Ina228Driver::readVBATDirect(&Wire, INA228_I2C_ADDR);
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
        inhero::prepareRadioForSystemOff(false);

        // INA228 → Shutdown mode with readback verification
        for (int retry = 0; retry < 3; retry++) {
          Wire.beginTransmission(INA228_I2C_ADDR);
          Wire.write(0x01);  // ADC_CONFIG register
          Wire.write(0x00);  // Shutdown (MSB)
          Wire.write(0x00);  // (LSB)
          if (Wire.endTransmission() != 0) {
            delay(10);
            continue;
          }
          delay(2);
          Wire.beginTransmission(INA228_I2C_ADDR);
          Wire.write(0x01);
          Wire.endTransmission(false);
          Wire.requestFrom((uint8_t)INA228_I2C_ADDR, (uint8_t)2);
          uint16_t rb = 0;
          if (Wire.available() >= 2) {
            rb = (Wire.read() << 8) | Wire.read();
          }
          if ((rb & 0xF000) == 0x0000) break;
          delay(10);
        }

        // Read DIAG_ALRT to clear any latched alert flag
        Wire.beginTransmission(INA228_I2C_ADDR);
        Wire.write(0x0B);
        Wire.endTransmission(false);
        Wire.requestFrom((uint8_t)INA228_I2C_ADDR, (uint8_t)2);
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
        {
          const uint8_t mask_regs[] = {0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D};
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
        Wire.beginTransmission(BME280_I2C_ADDR);
        Wire.write(0xF4);  // ctrl_meas
        Wire.write(0x00);  // Sleep mode
        Wire.endTransmission();

        configureRTCWake(LOW_VOLTAGE_SLEEP_MINUTES);
        NRF_P0->LATCH = (1UL << RTC_INT_PIN);

        Wire.end();
        inhero::disconnectLeakyPullups();
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

  // Enable DC/DC REG1 (VDD 3.3V → 1.3V core, ~1.5mA saving). REG0 not needed —
  // RAK4630 is powered from TPS62840 VDD, not VBUS. Done after peripheral init.
  NRF52BoardDCDC::begin();

  // LEDs already initialized in boardConfig.begin()
  // Blue LED was used for boot sequence visualization
  // Red LED indicates missing components (if blinking)

  // Start hardware watchdog (600s timeout)
  // Must be last - after all initializations are complete
  BoardConfigContainer::setupWatchdog();

  // Set initial USB IINDPM limit based on VBUS state at boot
  if (inhero::isUsbPowered()) {
    BoardConfigContainer::setUsbConnected(true);
  }
}

void InheroMr2Board::tick() {
  inhero::serviceUsbAutoManagement();

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

  // Solution: When we have a valid Coulomb-counted SOC, we reverse-map it to
  // the Li-Ion 1S OCV (Open Circuit Voltage) that the app expects.
  // This way the app always displays our accurate chemistry-independent SOC.

  // TODO: Remove this workaround once MeshCore supports transmitting the actual
  // SOC percentage alongside (or instead of) battery millivolts. At that point,
  // this function should return the real battery voltage again.

  const BatterySOCStats* socStats = boardConfig.getSOCStats();
  if (socStats && socStats->soc_valid) {
    return inhero::socToLiIonMilliVolts(boardConfig.getStateOfCharge());
  }

  // Fallback: no valid Coulomb-counting SOC yet — return real voltage
  const Telemetry* telemetry = boardConfig.getTelemetryData();
  if (!telemetry) {
    return 0;
  }
  return telemetry->battery.voltage;
}

bool InheroMr2Board::startOTAUpdate(const char* id, char reply[]) {
  // Skip in-app BLE DFU (unstable here) and jump to the Adafruit bootloader's
  // native OTA DFU via enterOTADfu() (sets GPREGRET=0xA8 + reset).
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

// Collects board telemetry and appends to CayenneLPP packet
bool InheroMr2Board::queryBoardTelemetry(CayenneLPP& telemetry) {
  return inhero::appendBoardTelemetry(boardConfig, telemetry);
}

// Handles custom CLI getter commands for board configuration
bool InheroMr2Board::getCustomGetter(const char* getCommand, char* reply, uint32_t maxlen) {
  return inhero::handleGet(boardConfig, getCommand, reply, maxlen);
}

// Handles custom CLI setter commands for board configuration
const char* InheroMr2Board::setCustomSetter(const char* setCommand) {
  return inhero::handleSet(boardConfig, setCommand);
}

// ===== Power Management Methods (Rev 1.1) =====

// Get low-voltage sleep threshold (chemistry-specific)
uint16_t InheroMr2Board::getLowVoltageSleepThreshold() {
  BoardConfigContainer::BatteryType chemType = boardConfig.getBatteryType();
  return BoardConfigContainer::getLowVoltageSleepThreshold(chemType);
}

// Get low-voltage wake threshold (chemistry-specific)
uint16_t InheroMr2Board::getLowVoltageWakeThreshold() {
  BoardConfigContainer::BatteryType chemType = boardConfig.getBatteryType();
  return BoardConfigContainer::getLowVoltageWakeThreshold(chemType);
}

// Initiate controlled shutdown with filesystem protection (Rev 1.1)

// Rev 1.1 low-voltage shutdown uses System Sleep with GPIO latch (< 500µA total):
// - INA228 enters shutdown mode (~3.5µA)
// - BQ CE pin latched HIGH via FET (solar charging continues autonomously)
// - RTC countdown timer configured for periodic wake
// - nRF52 enters SYSTEMOFF (~1.5µA) — GPIO latches preserved
// - RTC wake triggers reboot; Early Boot checks voltage for boot vs sleep-again
void InheroMr2Board::initiateShutdown(uint8_t reason) {
  MESH_DEBUG_PRINTLN("PWRMGT: Initiating shutdown (reason=0x%02X)", reason);

  // 1. Stop background tasks to prevent filesystem corruption
  BoardConfigContainer::stopBackgroundTasks();

  // 2. INA228: Shutdown mode to minimize sleep current (~3.5µA vs ~300µA continuous)
  //    No BUVL monitoring needed in sleep — RTC wakes us for voltage check.
  Ina228Driver* ina = boardConfig.getIna228Driver();
  if (ina) {
    // Release ALERT pin: latched LOW after LV trip wastes ~330µA through the
    // RAK4630 pull-up. Clear ALATCH + BUVL (transparent mode) before ADC shutdown.
    ina->enableAlert(false, false, false);  // DIAG_ALRT=0: ALATCH=0, clear all flags
    ina->setUnderVoltageAlert(0);           // BUVL=0: disable under-voltage comparison
    ina->shutdown();
  }

  // 3. SX1262 sleep + SPI cleanup (prevents ~4mA leakage in System Sleep)
  inhero::prepareRadioForSystemOff();

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

    // 5b. INA228 + BQ25798 \u2192 minimum sleep current. Must be AFTER CE=HIGH
    // (charge enable may re-enable BQ ADC). Repeats INA228 shutdown via raw I2C
    // with readback as a safety net if the driver call in step 2 silently failed.
    inhero::prepareIcsForSystemOff();

    // 5c. BME280 @ 0x76 — Force Sleep mode (saves ~1-7µA)
    // After normal operation readBmeTemperature() may have left BME280 in NORMAL mode.
    // Harmless NACK if no BME280 populated.
    Wire.beginTransmission(BME280_I2C_ADDR);
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
    inhero::disconnectLeakyPullups();

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
  inhero::disconnectLeakyPullups();
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

void InheroMr2Board::configureRTCWake(uint32_t minutes) {
  uint16_t ticks = static_cast<uint16_t>(
      minutes == 0 ? LOW_VOLTAGE_SLEEP_MINUTES
                   : (minutes > 4095 ? 4095 : minutes));
  inhero::configurePeriodicWake(ticks);
}

void InheroMr2Board::rtcInterruptHandler() {
  // Defer I2C work to the main loop to avoid ISR I2C collisions.
  rtc_irq_pending = true;
}
