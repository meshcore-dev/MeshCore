/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * Board Configuration Container Implementation
 */
#include "BoardConfigContainer.h"
#include "GuardedRTCClock.h"
#include "InheroMr2Board.h"
#include "target.h"

#include "lib/BqDriver.h"
#include "lib/Ina228Driver.h"
#include "lib/SimplePreferences.h"

#include <ArduinoJson.h>
#include <FreeRTOS.h>
#include <task.h>
#include <MeshCore.h>
#include <nrf_wdt.h>
#include <nrf_soc.h>  // For NRF_POWER (GPREGRET2)

#if ENV_INCLUDE_BME280
#include <Adafruit_BME280.h>
#endif

// rtc_clock is defined in target.cpp
extern GuardedRTCClock rtc_clock;

// Helper function to get RTC time safely
namespace {
  inline uint32_t getRTCTime() {
    return ((mesh::RTCClock&)rtc_clock).getCurrentTime();
  }

  bool isRtcPeriodicWakeConfigured(uint16_t expected_minutes);
  void configureRtcPeriodicWake(uint16_t minutes);

  bool isRtcPeriodicWakeConfigured(uint16_t expected_minutes) {
    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_CTRL1);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(RTC_I2C_ADDR, (uint8_t)1) != 1) return false;
    uint8_t ctrl1 = Wire.read();

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_CTRL2);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(RTC_I2C_ADDR, (uint8_t)1) != 1) return false;
    uint8_t ctrl2 = Wire.read();

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_TIMER_VALUE_0);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(RTC_I2C_ADDR, (uint8_t)2) != 2) return false;
    uint8_t val0 = Wire.read();
    uint8_t val1 = Wire.read();

    uint16_t countdown = (uint16_t)val0 | ((uint16_t)(val1 & 0x0F) << 8);

    bool timer_enabled = (ctrl1 & 0x04) != 0;    // TE
    bool repeat_enabled = (ctrl1 & 0x80) != 0;   // TRPT
    bool one_over_60_hz = (ctrl1 & 0x03) == 0x03; // TD=11 (1/60 Hz)
    bool interrupt_enabled = (ctrl2 & 0x10) != 0; // TIE

    return timer_enabled && repeat_enabled && one_over_60_hz && interrupt_enabled &&
         countdown == expected_minutes;
  }

  void configureRtcPeriodicWake(uint16_t minutes) {
    rtc_clock.setLocked(true);

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_CTRL1);
    Wire.write(0x00);  // TE=0, TD=00 (stop timer)
    Wire.endTransmission();

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_CTRL2);
    Wire.write(0x00);  // TIE=0
    Wire.endTransmission();

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_STATUS);
    Wire.write(0x00);  // Clear TF
    Wire.endTransmission();

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_TIMER_VALUE_0);
    uint16_t ticks = (minutes == 0) ? 1 : minutes;
    Wire.write(ticks & 0xFF);
    Wire.write((ticks >> 8) & 0x0F);
    Wire.endTransmission();

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_CTRL1);
    Wire.write(0x87);  // TE=1, TD=11 (1/60 Hz), TRPT=1 (repeat)
    Wire.endTransmission();

    Wire.beginTransmission(RTC_I2C_ADDR);
    Wire.write(RV3028_REG_CTRL2);
    Wire.write(0x10);  // TIE=1
    Wire.endTransmission();

    rtc_clock.setLocked(false);
  }

  void blinkRed(uint8_t count, uint16_t on_ms, uint16_t off_ms, bool led_enabled) {
    if (!led_enabled) {
      return;
    }
    for (uint8_t i = 0; i < count; i++) {
      digitalWrite(LED_RED, HIGH);
      delay(on_ms);
      digitalWrite(LED_RED, LOW);
      delay(off_ms);
    }
  }
}

// Hardware drivers
static BqDriver bq;
static Ina228Driver ina228(0x40);  // A0=GND, A1=GND

static SimplePreferences prefs;

// Forward declare board instance
extern InheroMr2Board board;

// Initialize singleton pointer
BqDriver* BoardConfigContainer::bqDriverInstance = nullptr;
Ina228Driver* BoardConfigContainer::ina228DriverInstance = nullptr;
TaskHandle_t BoardConfigContainer::heartbeatTaskHandle = NULL;
volatile bool BoardConfigContainer::lowVoltageAlertFired = false;
MpptStatistics BoardConfigContainer::mpptStats = {};
BatterySOCStats BoardConfigContainer::socStats = {};
BoardConfigContainer::BatteryType BoardConfigContainer::cachedBatteryType = BAT_UNKNOWN;
bool BoardConfigContainer::leds_enabled = true;  // Default: enabled
bool BoardConfigContainer::usbInputActive = false;  // Default: no USB connected
float BoardConfigContainer::tcCalOffset = 0.0f;  // Default: no temperature calibration offset
float BoardConfigContainer::lastValidBatteryTemp = 25.0f;  // Default: 25°C (= no derating until first valid reading)
uint32_t BoardConfigContainer::lastTempUpdateMs = 0;  // 0 = never updated

// Battery voltage thresholds moved to BatteryProperties structure (see .h file)
// Rev 1.1: INA228 ALERT pin (P1.02) triggers low-voltage sleep via ISR → volatile flag → tickPeriodic().
// No hardware UVLO (TPS EN tied to VDD). Low-voltage handling is always active when battery configured.

// Watchdog state
static bool wdt_enabled = false;

// PG-Stuck recovery: timestamp of last HIZ toggle (0 = never)
static uint32_t lastPgStuckToggleTime = 0;
#define PG_STUCK_COOLDOWN_MS (5 * 60 * 1000)  // 5 minutes between toggles

/// @brief Initialize and start the hardware watchdog timer
/// @details Configures nRF52 WDT with 600 second timeout for OTA compatibility. Only enabled in release builds.
///          Watchdog continues running during sleep and pauses during debug.
void BoardConfigContainer::setupWatchdog() {
  #ifndef DEBUG_MODE  // Only activate in release builds
    NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) |     // Run during sleep
                      (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);     // Pause during debug
    NRF_WDT->CRV = 32768 * 600;  // 600 seconds (10 min) @ 32.768 kHz - allows OTA updates
    NRF_WDT->RREN = WDT_RREN_RR0_Enabled << WDT_RREN_RR0_Pos;  // Enable reload register 0
    NRF_WDT->TASKS_START = 1;    // Start watchdog
    wdt_enabled = true;
    MESH_DEBUG_PRINTLN("Watchdog enabled: 600s timeout");
    
    // Visual feedback: blink LED 3 times to indicate WDT is active
    #ifdef LED_BLUE
      if (leds_enabled) {
        for (int i = 0; i < 3; i++) {
          digitalWrite(LED_BLUE, HIGH);
          delay(100);
          digitalWrite(LED_BLUE, LOW);
          delay(100);
        }
      }
    #endif
  #else
    MESH_DEBUG_PRINTLN("Watchdog disabled (DEBUG_MODE)");
  #endif
}

/// @brief Feed the watchdog timer to prevent system reset
/// @details Should be called regularly from main loop. No-op in debug builds.
void BoardConfigContainer::feedWatchdog() {
  #ifndef DEBUG_MODE
    if (wdt_enabled) {
      NRF_WDT->RR[0] = WDT_RR_RR_Reload;  // Reload watchdog
    }
  #endif
}

/// @brief Disable the watchdog timer (for OTA updates)
/// @details Note: nRF52 WDT cannot be stopped once started. This only sets flag to stop feeding.
void BoardConfigContainer::disableWatchdog() {
  #ifndef DEBUG_MODE
    wdt_enabled = false;  // Stop feeding the watchdog
  #endif
}

/// @brief Re-enables MPPT if BQ25798 disabled it (e.g., during !PG state)
/// @details BQ25798 does not persist MPPT=1 and automatically sets MPPT=0 when PG=0.
///          This function restores MPPT=1 when PG returns to 1.
///          
///          CRITICAL: Only runs when PowerGood=1 to avoid false positives.
///          Exception: PG-Stuck recovery toggles HIZ when VBUS is present but PG=0.
void BoardConfigContainer::checkAndFixSolarLogic() {
  if (!bqDriverInstance) return;

  // Check if MPPT is enabled in configuration
  bool mpptEnabled;
  BoardConfigContainer::loadMpptEnabled(mpptEnabled);

  if (!mpptEnabled) {
    // MPPT disabled in config - only disable if currently enabled (avoid unnecessary writes)
    uint8_t mpptVal = bqDriverInstance->readReg(0x15);
    if ((mpptVal & 0x01) != 0) {
      bqDriverInstance->writeReg(0x15, mpptVal & ~0x01);
      MESH_DEBUG_PRINTLN("MPPT disabled via config");
    }
    return;
  }

  // Check if PowerGood is currently set
  bool powerGood = bqDriverInstance->getChargerStatusPowerGood();

  if (!powerGood) {
    // PG-Stuck recovery: Panel may be connected but BQ didn't qualify it.
    // Typical at sunrise when VBUS ramps slowly past the input threshold.
    // Toggling HIZ forces a new input source qualification cycle (per datasheet).
    // Cooldown: max once per 5 minutes to prevent excessive toggling
    uint32_t now = millis();
    if (lastPgStuckToggleTime != 0 && (now - lastPgStuckToggleTime) < PG_STUCK_COOLDOWN_MS) {
      return;
    }

    uint16_t vbus_mv = bqDriverInstance->getVBUS();
    if (vbus_mv >= PG_STUCK_VBUS_THRESHOLD_MV) {
      bqDriverInstance->setHIZMode(true);
      delay(50);  // BQ needs time to enter HIZ and reset input detection
      bqDriverInstance->setHIZMode(false);
      lastPgStuckToggleTime = now;
      MESH_DEBUG_PRINTLN("PG-Stuck recovery: VBUS=%dmV but PG=0, toggled HIZ", vbus_mv);
    }
    return;
  }

  // Re-enable MPPT when PGOOD=1
  uint8_t mpptVal = bqDriverInstance->readReg(0x15);

  if ((mpptVal & 0x01) == 0) {
    bqDriverInstance->writeReg(0x15, mpptVal | 0x01);
    MESH_DEBUG_PRINTLN("MPPT re-enabled via register");
  }
}

/// @brief Single MPPT cycle — called from tickPeriodic() every 60s
/// @details Checks solar logic and updates MPPT stats.
void BoardConfigContainer::runMpptCycle() {
    // Clear any pending BQ25798 interrupt flags (even though INT pin is not used)
    // Flag registers (0x22-0x27) are read-to-clear and de-assert INT pin.
    if (bqDriverInstance) {
      bqDriverInstance->readReg(0x22); // CHARGER_FLAG_0 — read-to-clear
      bqDriverInstance->readReg(0x23); // CHARGER_FLAG_1
      bqDriverInstance->readReg(0x24); // CHARGER_FLAG_2
      bqDriverInstance->readReg(0x25); // CHARGER_FLAG_3
      bqDriverInstance->readReg(0x26); // FAULT_FLAG_0
      bqDriverInstance->readReg(0x27); // FAULT_FLAG_1
    }

    checkAndFixSolarLogic();
    bool mpptEnabled;
    BoardConfigContainer::loadMpptEnabled(mpptEnabled);
    if (mpptEnabled && bqDriverInstance) {
      updateMpptStats();
    }
}

/// @brief Stops heartbeat task and disarms alerts before OTA
/// @details MPPT and SOC work are tick-based (no tasks to stop).
///          Only the heartbeat LED task and INA228 alert need cleanup.
void BoardConfigContainer::stopBackgroundTasks() {
  MESH_DEBUG_PRINTLN("Stopping background tasks for OTA...");
  
  // Delete heartbeat task if running
  if (heartbeatTaskHandle != NULL) {
    vTaskDelete(heartbeatTaskHandle);
    heartbeatTaskHandle = NULL;
    MESH_DEBUG_PRINTLN("Heartbeat task stopped");
  }
  
  // Disarm INA228 low-voltage alert (Rev 1.1)
  disarmLowVoltageAlert();
  
  delay(200);
  MESH_DEBUG_PRINTLN("Background cleanup complete");
}

void BoardConfigContainer::heartbeatTask(void* pvParameters) {
  (void)pvParameters;

  pinMode(LED_BLUE, OUTPUT);

  while (true) {
    if (leds_enabled) {
      digitalWrite(LED_BLUE, HIGH);
    }
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms flash - well visible, minimal power
    if (leds_enabled) {
      digitalWrite(LED_BLUE, LOW);
    }
    vTaskDelay(pdMS_TO_TICKS(5000)); // 5s interval - lower power consumption
  }
}

/// @brief Enable or disable heartbeat LED and BQ25798 stat LED
/// @param enabled true = LEDs enabled, false = LEDs disabled
/// @return true on success
bool BoardConfigContainer::setLEDsEnabled(bool enabled) {
  leds_enabled = enabled;
  
  // Save to filesystem
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  prefs.putString("leds_en", enabled ? "1" : "0");
  prefs.end();
  
  // Control heartbeat task
  if (enabled) {
    // Start heartbeat if not running
    if (heartbeatTaskHandle == NULL) {
      xTaskCreate(heartbeatTask, "Heartbeat", 512, NULL, 1, &heartbeatTaskHandle);
    }
  } else {
    // Stop heartbeat task
    if (heartbeatTaskHandle != NULL) {
      vTaskDelete(heartbeatTaskHandle);
      heartbeatTaskHandle = NULL;
      // Turn off LED
      pinMode(LED_BLUE, OUTPUT);
      digitalWrite(LED_BLUE, LOW);
    }
  }
  
  // Control BQ25798 STAT LED (only if BQ is initialized)
  if (BQ_INITIALIZED && bqDriverInstance) {
    bqDriverInstance->setStatPinEnable(enabled);
  }
  
  return true;
}

/// @brief Get current LED enable state
/// @return true if LEDs are enabled
bool BoardConfigContainer::getLEDsEnabled() const {
  return leds_enabled;
}

/// @brief Updates MPPT statistics based on elapsed time and current status
/// Should be called when MPPT status changes or periodically for time accounting
void BoardConfigContainer::updateMpptStats() {
  if (!bqDriverInstance) return;
  
  static bool lastMpptStatus = false;
  static bool initialized = false;
  
  // Get current time - prefer RTC, fallback to millis()
  uint32_t currentTime;
  uint32_t rtcTime = getRTCTime();
  
  // Check if RTC is initialized (returns > 0 if time was set)
  // AutoDiscoverRTCClock returns 0 if no RTC found and time not set
  if (rtcTime > 1000000000) { // Sanity check: After year 2001
    currentTime = rtcTime;
    if (!mpptStats.usingRTC) {
      // Switch from millis to RTC
      mpptStats.usingRTC = true;
      mpptStats.lastUpdateTime = currentTime;
      lastMpptStatus = bqDriverInstance->getMPPTenable();
      initialized = true;
      return; // Reset timing on switch
    }
  } else {
    // RTC not available or not set - use millis() in seconds
    currentTime = millis() / 1000;
  }
  
  bool currentMpptStatus = bqDriverInstance->getMPPTenable();
  
  // Initialize on first run
  if (!initialized) {
    mpptStats.lastUpdateTime = currentTime;
    lastMpptStatus = currentMpptStatus;
    initialized = true;
    return;
  }
  
  // Calculate elapsed time since last update
  uint32_t elapsedSeconds = currentTime - mpptStats.lastUpdateTime;
  
  // Sanity check: If more than 48 hours passed, reset
  const uint32_t MAX_INTERVAL_SEC = 48UL * 60UL * 60UL;
  if (elapsedSeconds > MAX_INTERVAL_SEC) {
    mpptStats.lastUpdateTime = currentTime;
    lastMpptStatus = currentMpptStatus;
    return;
  }
  
  uint32_t elapsedMinutes = elapsedSeconds / 60;
  
  if (elapsedMinutes == 0 && lastMpptStatus == currentMpptStatus) {
    return; // No time passed and no status change
  }
  
  // Add time to current hour accumulator if MPPT was enabled
  if (lastMpptStatus && elapsedMinutes > 0) {
    mpptStats.currentHourMinutes += elapsedMinutes;
    if (mpptStats.currentHourMinutes > 60) {
      mpptStats.currentHourMinutes = 60; // Cap at 60 minutes per hour
    }
  }
  
  // Calculate energy harvested since last update if MPPT was enabled
  if (lastMpptStatus && elapsedSeconds > 0) {
    // Use last measured power and integrate over time: E = P × t
    // Energy in mWh = Power in mW × Time in hours
    float hours = elapsedSeconds / 3600.0f;
    uint32_t energy_mWh = (uint32_t)(mpptStats.lastPower_mW * hours);
    mpptStats.currentHourEnergy_mWh += energy_mWh;
  }
  
  // Sample current solar power for next integration period
  if (currentMpptStatus) {
    uint16_t vbat_mppt = ina228DriverInstance ? ina228DriverInstance->readVoltage_mV() : 0;
    const Telemetry* telem = bqDriverInstance->getTelemetryData(vbat_mppt);
    if (telem) {
      // Calculate power: P = U * I (both in mV and mA, result in mW)
      mpptStats.lastPower_mW = (int32_t)telem->solar.voltage * telem->solar.current / 1000;
    }
  } else {
    mpptStats.lastPower_mW = 0; // No power when MPPT disabled
  }
  
  mpptStats.lastUpdateTime = currentTime;
  lastMpptStatus = currentMpptStatus;
  
  // Check if we need to move to the next hour
  static uint32_t lastHourCheck = 0;
  uint32_t currentHour = currentTime / 3600;
  uint32_t lastHour = lastHourCheck / 3600;
  
  if (currentHour > lastHour) {
    // Store the completed hour's data
    mpptStats.hours[mpptStats.currentIndex].mpptEnabledMinutes = mpptStats.currentHourMinutes;
    mpptStats.hours[mpptStats.currentIndex].timestamp = currentTime;
    mpptStats.hours[mpptStats.currentIndex].harvestedEnergy_mWh = mpptStats.currentHourEnergy_mWh;
    
    // Move to next index (circular buffer)
    mpptStats.currentIndex = (mpptStats.currentIndex + 1) % MPPT_STATS_HOURS;
    
    // Reset for new hour
    mpptStats.currentHourMinutes = 0;
    mpptStats.currentHourEnergy_mWh = 0;
    lastHourCheck = currentTime;
  }
}

/// @brief Returns current max charge current as string
/// @return Static string buffer with charge current in mA
const char* BoardConfigContainer::getChargeCurrentAsStr() {
  static char buffer[16];
  snprintf(buffer, sizeof(buffer), "%dmA", this->getMaxChargeCurrent_mA());
  return buffer;
}

/// @brief Writes charger status information into provided buffer
/// @param buffer Destination buffer for status string
/// @param bufferSize Size of destination buffer
void BoardConfigContainer::getChargerInfo(char* buffer, uint32_t bufferSize) {
  // Check if buffer is valid
  if (!buffer || bufferSize == 0) {
    return;
  }
  
  // Clear buffer to prevent garbage data
  memset(buffer, 0, bufferSize);
  
  // Check if BQ25798 is initialized and responsive
  if (!BQ_INITIALIZED) {
    snprintf(buffer, bufferSize, "BQ25798 not initialized");
    return;
  }
  
  const char* powerGood = bq.getChargerStatusPowerGood() ? "PG" : "!PG";
  const char* statusString = "Unknown";  // Initialize with default value
  bq25798_charging_status status = bq.getChargingStatus();
  
  switch (status) {
  case bq25798_charging_status::BQ25798_CHARGER_STATE_NOT_CHARGING: {
    statusString = "!CHG";
    break;
  }
  case bq25798_charging_status::BQ25798_CHARGER_STATE_PRE_CHARGING: {
    statusString = "PRE";
    break;
  }
  case bq25798_charging_status::BQ25798_CHARGER_STATE_CC_CHARGING: {
    statusString = "CC";
    break;
  }
  case bq25798_charging_status::BQ25798_CHARGER_STATE_CV_CHARGING: {
    statusString = "CV";
    break;
  }
  case bq25798_charging_status::BQ25798_CHARGER_STATE_TRICKLE_CHARGING: {
    statusString = "TRICKLE";
    break;
  }
  case bq25798_charging_status::BQ25798_CHARGER_STATE_TOP_OF_TIMER_ACTIVE_CHARGING: {
    statusString = "TOP";
    break;
  }
  case bq25798_charging_status::BQ25798_CHARGER_STATE_DONE_CHARGING: {
    statusString = "DONE";
    break;
  }
  default:
    statusString = "Unknown";
    break;
  }
  
  if (lastPgStuckToggleTime == 0) {
    snprintf(buffer, bufferSize, "%s / %s HIZ:never", powerGood, statusString);
  } else {
    uint32_t agoSec = (millis() - lastPgStuckToggleTime) / 1000;
    if (agoSec < 60) {
      snprintf(buffer, bufferSize, "%s / %s HIZ:%ds ago", powerGood, statusString, agoSec);
    } else if (agoSec < 3600) {
      snprintf(buffer, bufferSize, "%s / %s HIZ:%dm ago", powerGood, statusString, agoSec / 60);
    } else {
      snprintf(buffer, bufferSize, "%s / %s HIZ:%dh ago", powerGood, statusString, agoSec / 3600);
    }
  }
}

/// @brief Reads BQ25798 status/fault registers and produces a compact diagnostic string
/// @details Register layout (BQ25798 datasheet SLUSDV2B):
///   0x1B STATUS_0: IINDPM[7] VINDPM[6] WD[5] rsvd[4] PG[3] AC2[2] AC1[1] VBUS[0]
///   0x1C STATUS_1: CHG_STAT[7:5] VBUS_STAT[4:1] BC12[0]
///   0x1D STATUS_2: ICO[7:6] rsvd[5:3] TREG[2] DPDM[1] VBAT_PRESENT[0]
///   0x1E STATUS_3: ACRB2[7] ACRB1[6] ADC_DONE[5] VSYS[4] CHG_TMR[3] TRICHG_TMR[2] PRECHG_TMR[1] rsvd[0]
///   0x1F STATUS_4: rsvd[7:5] VBATOTG_LOW[4] TS_COLD[3] TS_COOL[2] TS_WARM[1] TS_HOT[0]
///   0x20 FAULT_0:  IBAT_REG[7] VBUS_OVP[6] VBAT_OVP[5] IBUS_OCP[4] IBAT_OCP[3] CONV_OCP[2] VAC2_OVP[1] VAC1_OVP[0]
///   0x21 FAULT_1:  rsvd[7] OTG_UVP[6] OTG_OVP[5] rsvd[4] VSYS_SHORT[3] VSYS_OVP[2] rsvd[1:0]
///   0x0F CTRL_0:   AUTO_IBATDIS[7] FORCE_IBATDIS[6] EN_CHG[5] EN_ICO[4] FORCE_ICO[3] EN_HIZ[2] EN_TERM[1] EN_BACKUP[0]
///   0x18 NTC_1:    TS_COOL[7:6] TS_WARM[5:4] BHOT[3:2] BCOLD[1] TS_IGNORE[0]
/// @param buffer Destination buffer (min 100 bytes recommended)
/// @param bufferSize Size of destination buffer
bool BoardConfigContainer::probeRtc() {
  // Address ACK
  Wire.beginTransmission(0x52);
  if (Wire.endTransmission() != 0) return false;

  // User-RAM 0x1F write/readback with two patterns (catches stuck bits).
  // Save original first, restore afterwards — keeps user data intact.
  Wire.beginTransmission(0x52);
  Wire.write(0x1F);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)0x52, (uint8_t)1) != 1) return false;
  uint8_t saved = Wire.read();

  for (uint8_t pat : {0xA5, 0x5A}) {
    Wire.beginTransmission(0x52);
    Wire.write(0x1F);
    Wire.write(pat);
    if (Wire.endTransmission() != 0) return false;

    Wire.beginTransmission(0x52);
    Wire.write(0x1F);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)0x52, (uint8_t)1) != 1) return false;
    if (Wire.read() != pat) return false;
  }

  // Restore original byte
  Wire.beginTransmission(0x52);
  Wire.write(0x1F);
  Wire.write(saved);
  Wire.endTransmission();
  return true;
}

namespace {
  bool probeI2CAddr(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
  }
}

void BoardConfigContainer::getSelfTest(char* buffer, uint32_t bufferSize) {
  if (!buffer || bufferSize == 0) return;
  const char* ina = probeI2CAddr(0x40)             ? "OK" : "NACK";
  const char* bq  = probeI2CAddr(BQ25798_I2C_ADDR) ? "OK" : "NACK";
  const char* bme = probeI2CAddr(0x76)             ? "OK" : "NACK";

  // RTC: distinguish bus-NACK from write-failure
  const char* rtc;
  if (!probeI2CAddr(0x52)) {
    rtc = "NACK";
  } else if (!probeRtc()) {
    rtc = "WR_FAIL";
  } else {
    rtc = "OK";
  }

  snprintf(buffer, bufferSize, "INA:%s BQ:%s RTC:%s BME:%s", ina, bq, rtc, bme);
}

void BoardConfigContainer::getBqDiagnostics(char* buffer, uint32_t bufferSize) {
  if (!buffer || bufferSize == 0) return;
  memset(buffer, 0, bufferSize);

  if (!BQ_INITIALIZED) {
    snprintf(buffer, bufferSize, "BQ not init");
    return;
  }

  // Read status registers (read-only, safe to read)
  uint8_t s0 = bq.readReg(0x1B);  // CHARGER_STATUS_0
  uint8_t s1 = bq.readReg(0x1C);  // CHARGER_STATUS_1
  uint8_t s2 = bq.readReg(0x1D);  // CHARGER_STATUS_2
  uint8_t s3 = bq.readReg(0x1E);  // CHARGER_STATUS_3
  uint8_t s4 = bq.readReg(0x1F);  // CHARGER_STATUS_4
  uint8_t f0 = bq.readReg(0x20);  // FAULT_STATUS_0
  uint8_t f1 = bq.readReg(0x21);  // FAULT_STATUS_1

  // Read control registers
  uint8_t ctrl0 = bq.readReg(0x0F);  // CHARGER_CONTROL_0: EN_CHG[5], EN_HIZ[2]
  uint8_t ntc1  = bq.readReg(0x18);  // NTC_CONTROL_1

  // Decode TS region from STATUS_4 (0x1F): TS_COLD[3] TS_COOL[2] TS_WARM[1] TS_HOT[0]
  const char* ts_str = "OK";
  if (s4 & 0x01)      ts_str = "HOT";
  else if (s4 & 0x02) ts_str = "WARM";
  else if (s4 & 0x04) ts_str = "COOL";
  else if (s4 & 0x08) ts_str = "COLD";

  // Build active-flags substring (only show abnormal conditions)
  char flags[50] = "";
  int pos = 0;
  if (s0 & 0x80) pos += snprintf(flags + pos, sizeof(flags) - pos, " IINDPM");
  if (s0 & 0x40) pos += snprintf(flags + pos, sizeof(flags) - pos, " VINDPM");
  if (s0 & 0x20) pos += snprintf(flags + pos, sizeof(flags) - pos, " WD!");
  if (s2 & 0x04) pos += snprintf(flags + pos, sizeof(flags) - pos, " TREG");
  if (s3 & 0x08) pos += snprintf(flags + pos, sizeof(flags) - pos, " CHG_TMR");
  if (s3 & 0x04) pos += snprintf(flags + pos, sizeof(flags) - pos, " TCTMR");
  if (s3 & 0x02) pos += snprintf(flags + pos, sizeof(flags) - pos, " PCTMR");
  if (f0 & 0x40) pos += snprintf(flags + pos, sizeof(flags) - pos, " VBUS_OVP");
  if (f0 & 0x20) pos += snprintf(flags + pos, sizeof(flags) - pos, " VBAT_OVP");

  bool en_chg = (ctrl0 >> 5) & 1;  // EN_CHG: bit 5 of CHARGER_CONTROL_0
  bool en_hiz = (ctrl0 >> 2) & 1;  // EN_HIZ: bit 2 of CHARGER_CONTROL_0

  // Read actual IINDPM from REG06 for verification
  uint16_t iindpm_mA = (uint16_t)(bq.getInputLimitA() * 1000);

  // Compact output: TS region, active flags, control bits, IINDPM readback, faults, raw status hex, NTC config
  snprintf(buffer, bufferSize,
           "TS:%s%s CE:%d HIZ:%d IINDPM:%umA F:%02X/%02X S:%02X.%02X.%02X.%02X.%02X N:%02X",
           ts_str, flags, en_chg, en_hiz, iindpm_mA, f0, f1, s0, s1, s2, s3, s4, ntc1);
}

/// @brief Initializes battery manager, preferences, and background tasks
/// @return true if BQ25798 initialized successfully
bool BoardConfigContainer::begin() {
  // Initialize LEDs early for boot sequence visualization
  pinMode(LED_BLUE, OUTPUT);  // Blue LED (P1.03)
  pinMode(LED_RED, OUTPUT);   // Red LED (P1.04)
  digitalWrite(LED_BLUE, LOW);
  digitalWrite(LED_RED, LOW);
  
  // Load LED enable state from filesystem (default: enabled)
  SimplePreferences prefs_led;
  if (prefs_led.begin("inheromr2")) {
    char led_buffer[8];
    prefs_led.getString("leds_en", led_buffer, sizeof(led_buffer), "1");
    leds_enabled = (strcmp(led_buffer, "1") == 0);
    prefs_led.end();
  } else {
    leds_enabled = true;  // Default: enabled
  }

  bool skip_fs_writes = ((NRF_POWER->GPREGRET2 & 0x03) == SHUTDOWN_REASON_LOW_VOLTAGE);
  
  // === MR2 Hardware (Rev 1.1): INA228 Power Monitor with ALERT-based low-voltage sleep ===
  // MR2 uses INA228 at 0x40 (A0=GND, A1=GND)
  MESH_DEBUG_PRINTLN("=== INA228 Detection @ 0x40 ===");
  delay(10);  // Let serial output flush
  
  // Visual indicator: Red LED on = INA228 detection in progress
  if (leds_enabled) {
    digitalWrite(LED_RED, HIGH);
    delay(50);
  }
  
  // First test I2C communication
  Wire.beginTransmission(0x40);
  uint8_t i2c_result = Wire.endTransmission();
  MESH_DEBUG_PRINTLN("INA228: I2C probe result = %d (0=OK)", i2c_result);
  delay(10);
  
  if (i2c_result == 0) {
    // Device responds, read ID registers
    Wire.beginTransmission(0x40);
    Wire.write(0x3E);  // Manufacturer ID register
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x40, (uint8_t)2);
    if (Wire.available() >= 2) {
      uint16_t mfg_id = (Wire.read() << 8) | Wire.read();
      MESH_DEBUG_PRINTLN("INA228: MFG_ID = 0x%04X (expect 0x5449)", mfg_id);
      delay(10);
    }
    
    Wire.beginTransmission(0x40);
    Wire.write(0x3F);  // Device ID register
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x40, (uint8_t)2);
    if (Wire.available() >= 2) {
      uint16_t dev_id = (Wire.read() << 8) | Wire.read();
      MESH_DEBUG_PRINTLN("INA228: DEV_ID = 0x%04X (expect 0x0228)", dev_id);
      delay(10);
    }
    
    // Try to initialize
    if (ina228.begin(100.0f)) {  // 100mΩ shunt resistor (optimal SNR for 10mA standby / 1A max)
      INA228_INITIALIZED = true;
      ina228DriverInstance = &ina228;
      
      // Turn off red LED (INA228 detection complete)
      if (leds_enabled) {
        digitalWrite(LED_RED, LOW);
        delay(10);
      }
      
      // Blue LED flash: INA228 initialized
      if (leds_enabled) {
        digitalWrite(LED_BLUE, HIGH);
        delay(150);
        digitalWrite(LED_BLUE, LOW);
        delay(100);
      }
      
      // Arm INA228 low-voltage alert for this battery chemistry
      // Rev 1.1: Always active when battery type is configured (no CLI toggle)
      // ISR on ALERT pin → volatile flag → tickPeriodic() → System Sleep with GPIO latch
      armLowVoltageAlert();

      // NOTE: Low-voltage recovery SOC=0% is handled in InheroMr2Board::begin()
      // (after setLowVoltageRecovery()), not here, because lowVoltageRecovery isn't set yet.
    } else {
      MESH_DEBUG_PRINTLN("✗ INA228 begin() failed (check MFG_ID/DEV_ID above)");
      INA228_INITIALIZED = false;
    }
  } else {
    MESH_DEBUG_PRINTLN("✗ INA228 no I2C ACK @ 0x40");
    INA228_INITIALIZED = false;
  }
  delay(10);

  // Initialize BQ25798
  if (bq.begin()) {
    BQ_INITIALIZED = true;
    bqDriverInstance = &bq;
    MESH_DEBUG_PRINTLN("BQ25798 found. ");

    // Blue LED flash: BQ25798 initialized
    if (leds_enabled) {
      digitalWrite(LED_BLUE, HIGH);
      delay(150);
      digitalWrite(LED_BLUE, LOW);
      delay(100);
    }
  } else {
    MESH_DEBUG_PRINTLN("BQ25798 not found.");
    BQ_INITIALIZED = false;
  }
  
  // Load NTC temperature calibration offset (applies to all BQ temperature readings)
  float tc_offset = 0.0f;
  if (loadTcCalOffset(tc_offset)) {
    tcCalOffset = tc_offset;
    MESH_DEBUG_PRINTLN("TC calibration offset loaded: %+.2f C", tc_offset);
  } else {
    MESH_DEBUG_PRINTLN("TC using default calibration (0.0)");
  }
  
  // === RV-3028 RTC Initialization ===
  // Address probe + user-RAM write/readback test (catches "zombie" RTCs that
  // ACK on bus but reject writes — see probeRtc() for details).
  // Retry up to 3 times — after OTA/warm-reset the I2C bus may need recovery.
  bool rtc_initialized = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (probeRtc()) {
      rtc_initialized = true;
      MESH_DEBUG_PRINTLN("RV-3028 RTC OK (attempt %d)", attempt + 1);
      if (leds_enabled) {
        digitalWrite(LED_BLUE, HIGH);
        delay(150);
        digitalWrite(LED_BLUE, LOW);
        delay(100);
      }
      break;
    }
    MESH_DEBUG_PRINTLN("RV-3028 RTC self-test failed (attempt %d)", attempt + 1);
    delay(20);
  }
  
  // === MR2 Configuration ===
  SimplePreferences prefs_init;
  prefs_init.begin(PREFS_NAMESPACE);
  
  BatteryType bat = DEFAULT_BATTERY_TYPE;
  FrostChargeBehaviour frost = DEFAULT_FROST_BEHAVIOUR;
  uint16_t maxChargeCurrent_mA = DEFAULT_MAX_CHARGE_CURRENT_MA;

  if (!loadBatType(bat)) {
    if (!skip_fs_writes) {
      prefs_init.putString(BATTKEY, getBatteryTypeCommandString(bat));
    }
  }
  if (!loadFrost(frost)) {
    if (!skip_fs_writes) {
      prefs_init.putString(FROSTKEY, getFrostChargeBehaviourCommandString(frost));
    }
  }
  if (!loadMaxChrgI(maxChargeCurrent_mA)) {
    if (!skip_fs_writes) {
      prefs_init.putInt(MAXCHARGECURRENTKEY, maxChargeCurrent_mA);
    }
  }

  this->configureBaseBQ();
  this->configureChemistry(bat);
  cachedBatteryType = bat;  // Cache for static methods (updateBatterySOC, calculateTTL)

  // Charger active by default — HIZ-Gate removed (Rev 1.1 PCB stable).
  bq.setHIZMode(false);

  this->setFrostChargeBehaviour(frost);
  this->setMaxChargeCurrent_mA(maxChargeCurrent_mA);

  // Mask ALL BQ25798 interrupts — INT pin is not used (polling only).
  // Default mask registers are 0x00 (all unmasked!) → every event pulls INT LOW.
  // With INPUT_PULLUP on BQ_INT_PIN: LOW = ~254µA wasted through pull-up.
  bq.writeReg(0x28, 0xFF);  // Charger Mask 0 — mask all
  bq.writeReg(0x29, 0xFF);  // Charger Mask 1 — mask all
  bq.writeReg(0x2A, 0xFF);  // Charger Mask 2 — mask all
  bq.writeReg(0x2B, 0xFF);  // Charger Mask 3 — mask all
  bq.writeReg(0x2C, 0xFF);  // Fault Mask 0 — mask all
  bq.writeReg(0x2D, 0xFF);  // Fault Mask 1 — mask all

  // Clear any latched flag/interrupt status from previous operation/boot
  // Flag registers are read-to-clear and de-assert the INT pin.
  // (0x1B/0x20/0x21 are STATUS registers — read-only, do NOT clear flags!)
  bq.readReg(0x22); // CHARGER_FLAG_0 — read-to-clear
  bq.readReg(0x23); // CHARGER_FLAG_1
  bq.readReg(0x24); // CHARGER_FLAG_2
  bq.readReg(0x25); // CHARGER_FLAG_3
  bq.readReg(0x26); // FAULT_FLAG_0
  bq.readReg(0x27); // FAULT_FLAG_1

  // Heartbeat LED task (GPIO only — no I2C, safe as FreeRTOS task)
  if (heartbeatTaskHandle == NULL && leds_enabled) {
    BaseType_t taskCreated = xTaskCreate(BoardConfigContainer::heartbeatTask, "Heartbeat", 1024, NULL, 1, &heartbeatTaskHandle);
    if (taskCreated != pdPASS) {
      MESH_DEBUG_PRINTLN("Failed to create Heartbeat task!");
      return false;
    }
  }

  // BQ_INT_PIN no longer used — solar checks run via polling in tickPeriodic()
  // Pull up to prevent floating trace on PCB
  pinMode(BQ_INT_PIN, INPUT_PULLUP);

  // Check if all critical components initialized
  bool all_components_ok = BQ_INITIALIZED && INA228_INITIALIZED && rtc_initialized;
  
  if (!all_components_ok) {
    // Start permanent slow red LED blink to indicate missing component
    MESH_DEBUG_PRINTLN("⚠️ Missing components - starting error LED");
    if (!BQ_INITIALIZED) MESH_DEBUG_PRINTLN("  - BQ25798 missing");
    if (!INA228_INITIALIZED) MESH_DEBUG_PRINTLN("  - INA228 missing");
    if (!rtc_initialized) MESH_DEBUG_PRINTLN("  - RV-3028 RTC missing");
    
    // Create error LED blink task (GPIO only)
    if (leds_enabled) {
      xTaskCreate([](void* param) {
        while (1) {
          digitalWrite(LED_RED, HIGH);  // Red LED on
          vTaskDelay(pdMS_TO_TICKS(500));
          digitalWrite(LED_RED, LOW);   // Red LED off
          vTaskDelay(pdMS_TO_TICKS(500));
        }
      }, "ErrorLED", 512, NULL, 1, NULL);
    }
  }
  
  // MPPT, SOC updates, and voltage monitoring are handled in tickPeriodic()
  // (called from InheroMr2Board::tick() — no FreeRTOS tasks doing I2C)
  
  // Load battery capacity from preferences (or default based on chemistry)
  float cap_mah = 0.0f;
  loadBatteryCapacity(cap_mah);
  socStats.capacity_mah = cap_mah;
  socStats.nominal_voltage = getNominalVoltage(bat);
  MESH_DEBUG_PRINTLN("SOC: capacity=%.0f mAh, nominal=%.2f V", cap_mah, socStats.nominal_voltage);

  // MR2 requires BQ25798 + INA228 (RTC is optional for basic operation)
  return BQ_INITIALIZED && INA228_INITIALIZED;
}

/// @brief Loads battery type from preferences
/// @param type Reference to store loaded battery type
/// @return true if preference found and valid, false if default used
bool BoardConfigContainer::loadBatType(BatteryType& type) const {
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[10];
  if (prefs.getString(BATTKEY, buffer, sizeof(buffer), "") > 0) {
    type = this->getBatteryTypeFromCommandString(buffer);
    if (type != BAT_UNKNOWN) {
      return true;
    } else {
      type = DEFAULT_BATTERY_TYPE;
      return false;
    }
  }
  
  // No preference found - use default
  type = DEFAULT_BATTERY_TYPE;
  return false;
}

/// @brief Loads frost charge behavior from preferences
/// @param behaviour Reference to store loaded behavior
/// @return true if preference found and valid, false if default used
bool BoardConfigContainer::loadFrost(FrostChargeBehaviour& behaviour) const {
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[10];
  if (prefs.getString(FROSTKEY, buffer, sizeof(buffer), "") > 0) {
    behaviour = this->getFrostChargeBehaviourFromCommandString(buffer);
    if (behaviour != REDUCE_UNKNOWN) {
      return true;
    } else {
      behaviour = DEFAULT_FROST_BEHAVIOUR;
      return false;
    }
  }
  
  // No preference found - use default
  behaviour = DEFAULT_FROST_BEHAVIOUR;
  return false;
}

/// @brief Loads maximum charge current from preferences
/// @param maxCharge_mA Reference to store loaded current in mA
/// @return true if preference found and valid (1-3000mA), false if default used
bool BoardConfigContainer::loadMaxChrgI(uint16_t& maxCharge_mA) const {
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[10];

  if (prefs.getString(MAXCHARGECURRENTKEY, buffer, sizeof(buffer), "") > 0) {

    int val = atoi(buffer);
    // Bounds check: Reasonable charge current range
    if (val > 0 && val <= 3000) {  // Max 3A for safety
      maxCharge_mA = val;
      return true;
    } else {
      maxCharge_mA = DEFAULT_MAX_CHARGE_CURRENT_MA;
      return false;
    }
  }
  
  // No preference found - use default
  maxCharge_mA = DEFAULT_MAX_CHARGE_CURRENT_MA;
  return false;
}

/// @brief Loads MPPT enabled setting from preferences
/// @param enabled Reference to store loaded setting
/// @return true if preference found, false if default used
bool BoardConfigContainer::loadMpptEnabled(bool& enabled) {
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[10];

  if (prefs.getString("mpptEn", buffer, sizeof(buffer), "") > 0) {
    if (buffer[0] != '\0') {
      enabled = buffer[0] == '1' ? true : false;
      return true;
    } else {
      enabled = DEFAULT_MPPT_ENABLED;
      return false;
    }
  }
  
  // No preference found - use default
  enabled = DEFAULT_MPPT_ENABLED;
  return false;
}

/// @brief Returns combined telemetry from INA228 (battery) and BQ25798 (solar + temperature)
/// @note Battery voltage/current from INA228 (24-bit ADC, ±0.1% accuracy)
///                  Solar data and battery temperature from BQ25798 ADC
///
/// Temperature availability depends on power conditions:
///   VBUS > 3.4V  → BQ25798 ADC runs → temperature available
///   VBAT >= 3.2V → BQ25798 ADC runs → temperature available
///   VBAT < 3.2V  → TS channel disabled (datasheet 9.3.16) → temperature = N/A
///   VBAT < 2.9V  → ADC cannot operate at all → temperature = N/A, solar = 0
///
/// Temperature sentinel values (propagated from BqDriver::calculateBatteryTemp):
///   -999.0f = I2C communication error or NTC unavailable
///   -888.0f = ADC not ready / TS disabled due to low VBAT
///    -99.0f = NTC open circuit (disconnected)
///     99.0f = NTC short circuit
///   Values outside -50..+90°C are treated as invalid → displayed as "N/A"
const Telemetry* BoardConfigContainer::getTelemetryData() {
  static Telemetry telemetry;
  
  // Battery voltage/current ALWAYS from INA228 (no fallback to BQ25798)
  // INA228 for precise battery monitoring
  uint16_t batt_voltage = 0;
  float batt_current = 0.0f;
  int32_t batt_power = 0;
  if (ina228DriverInstance != nullptr) {
    batt_voltage = ina228DriverInstance->readVoltage_mV();
    batt_current = ina228DriverInstance->readCurrent_mA_precise();
    batt_power = (int32_t)((batt_voltage * batt_current) / 1000.0f);
  }

  // Get base telemetry from BQ25798 (solar data + temperature)
  // Pass VBAT so BqDriver can disable TS channel when VBAT < 3.2V
  // (BQ25798 ADC requires VBAT >= 3.2V with TS enabled, else ADC won't start)
  const Telemetry* bqData = bq.getTelemetryData(batt_voltage);
  if (!bqData) {
    memset(&telemetry, 0, sizeof(Telemetry));
    return &telemetry;
  }

  // Copy BQ25798 data (solar, system)
  telemetry.solar = bqData->solar;
  telemetry.system = bqData->system;

  // Temperature: BQ25798 TS ADC reads NTC via REGN-biased divider.
  // Error codes from calculateBatteryTemp: -999 (I2C), -888 (ADC not ready), -99 (open), 99 (short).
  // Valid NTC range: approx -40..+85 °C. Anything outside -50..+90 is treated as unavailable.
  float bqTemp = bqData->batterie.temperature;
  if (bqTemp >= -50.0f && bqTemp <= 90.0f) {
    telemetry.batterie.temperature = bqTemp + tcCalOffset;
    // Cache for temperature derating in updateBatterySOC() (static context)
    lastValidBatteryTemp = telemetry.batterie.temperature;
    lastTempUpdateMs = millis();
  } else {
    // NTC unavailable (no solar / I2C error / ADC not ready) → propagate sentinel
    telemetry.batterie.temperature = -999.0f;
  }

  telemetry.batterie.voltage = batt_voltage;
  telemetry.batterie.current = batt_current;
  telemetry.batterie.power = batt_power;

  return &telemetry;
}

/// @brief Configures base BQ25798 settings (timers, watchdog, input limits, MPPT)
/// @return true if BQ initialized and configuration successful
bool BoardConfigContainer::configureBaseBQ() {
  if (!BQ_INITIALIZED) {
    return false;
  }

  bq.setRechargeThreshOffsetV(.2);
  bq.setPrechargeTimerEnable(false);
  bq.setFastChargeTimerEnable(false);
  bq.setTsIgnore(false);
  bq.setWDT(BQ25798_WDT_DISABLE);
  bq.setExtILIMpin(false);  // Disable ILIM_HIZ pin clamp — IINDPM managed by software
  bq.setInputLimitA(IINDPM_MAX_A);  // Safe default before chemistry is known; updateSolarIINDPM() refines later
  bq.setICOEnable(false);  // Disable ICO — IINDPM is explicitly managed, ICO must not overwrite it

  bq.setVOCdelay(BQ25798_VOC_DLY_2S);
  bq.setVOCrate(BQ25798_VOC_RATE_2MIN);
  bq.setVOCpercent(BQ25798_VOC_PCT_81_25); // 81.25% matches Vmp/Voc of typical crystalline Si panels (~80-83%)
  bq.setAutoDPinsDetection(false);
  bq.setMPPTenable(true);

  bq.setMinSystemV(2.75);  // 2.75V = next valid step above 2.7V (250mV steps: 2.5, 2.75, 3.0...)
  bq.setStatPinEnable(leds_enabled);  // Configure STAT LED based on user preference
  bq.setTsCool(BQ25798_TS_COOL_5C);
  bq.setTsWarm(BQ25798_TS_WARM_55C);  // 37.7% REGN → ~52°C with Inhero divider (default 45°C was ~42°C)

  // JEITA WARM zone: keep VREG unchanged (no voltage reduction).
  // Default JEITA_VSET = VREG-400mV causes VBAT_OVP for LiFePO4 (3.5V - 0.4V = 3.1V, OVP at 3.22V)
  // and is unnecessarily conservative for Li-Ion (4.1V - 0.4V = 3.7V).
  // Both chemistries use safe charge voltages (4.1V / 3.5V), no reduction needed.
  bq.setJeitaVSet(BQ25798_JEITA_VSET_UNCHANGED);

  // Disable auto battery discharge during VBAT_OVP (EN_AUTO_IBATDIS).
  // POR default = enabled → BQ actively sinks 30mA from battery during OVP to lower VBAT.
  // With JEITA_VSET fixed, VBAT_OVP should no longer trigger. Belt-and-suspenders safety.
  bq.setAutoIBATDIS(false);

  // Flush stale ADC registers by running one discard conversion.
  // After reboot (e.g. low-voltage recovery), BQ25798 retains old ADC values
  // from before shutdown. A fresh one-shot ensures registers reflect actual state.
  bq.getTelemetryData(0);  // VBAT unknown at this point, assume sufficient

  return true;
}

/// @brief Configures battery chemistry-specific parameters (cell count, charge voltage)
/// @param type Battery chemistry type (LIION_1S, LIFEPO4_1S, LTO_2S, BAT_UNKNOWN)
/// @return true if configuration successful
bool BoardConfigContainer::configureChemistry(BatteryType type) {
  if (!BQ_INITIALIZED) {
    return false;
  }

  // Get battery properties from lookup table
  const BatteryProperties* props = getBatteryProperties(type);
  if (!props) {
    MESH_DEBUG_PRINTLN("ERROR: Invalid battery type");
    return false;
  }

  // Apply charge enable/disable based on battery type
  bq.setChargeEnable(props->charge_enable);

  // CE-Pin hardware safety: Only pull CE HIGH (enable charging via FET) when chemistry is known
  // Rev 1.1: DMN2004TK-7 N-FET inverts CE logic — HIGH=enable, LOW=disable
  // External pull-down ensures CE stays LOW (charging disabled) when RAK is off or unbooted
#ifdef BQ_CE_PIN
  pinMode(BQ_CE_PIN, OUTPUT);
  digitalWrite(BQ_CE_PIN, props->charge_enable ? HIGH : LOW);
  MESH_DEBUG_PRINTLN("BQ CE pin %s (charge_enable=%d)", props->charge_enable ? "HIGH (enabled via FET)" : "LOW (disabled via FET)", props->charge_enable);
#endif

  // Apply TS_IGNORE before potential early return — configureBaseBQ() resets it to false,
  // so chemistries with ts_ignore=true (BAT_UNKNOWN, LTO, NAION) need it set here.
  bq.setTsIgnore(props->ts_ignore);
  if (props->ts_ignore) {
    bq.setJeitaISetC(BQ25798_JEITA_ISETC_UNCHANGED);
    bq.setJeitaISetH(BQ25798_JEITA_ISETH_UNCHANGED);
  }

  if (!props->charge_enable) {
    MESH_DEBUG_PRINTLN("WARNING: Battery type UNKNOWN - Charging DISABLED for safety!");
    return true;  // No further configuration needed for unknown battery
  }

  // Configure chemistry-specific parameters
  // Configure cell count
  bq25798_cell_count_t cellCount = (type == BoardConfigContainer::BatteryType::LTO_2S) ? BQ25798_CELL_COUNT_2S : BQ25798_CELL_COUNT_1S;
  bq.setCellCount(cellCount);

  bq.setChargeLimitV(props->charge_voltage);

  return true;
}

/// @brief Gets current battery type from preferences
/// @return Current battery chemistry type, defaults to DEFAULT_BATTERY_TYPE if read fails
BoardConfigContainer::BatteryType BoardConfigContainer::getBatteryType() const {
  BatteryType bat;
  if (loadBatType(bat)) {
    return bat;
  } else {
    return DEFAULT_BATTERY_TYPE;
  }
}

/// @brief Gets current frost charge behavior from preferences
/// @return Frost charge behavior, defaults to NO_CHARGE if read fails
BoardConfigContainer::FrostChargeBehaviour BoardConfigContainer::getFrostChargeBehaviour() const {
  FrostChargeBehaviour frost;
  if (loadFrost(frost)) {
    return frost;
  } else {
    return NO_CHARGE;
  }
}

/// @brief Gets maximum charge current from preferences
/// @return Maximum charge current in mA, defaults to DEFAULT_MAX_CHARGE_CURRENT_MA (200mA) if read fails
uint16_t BoardConfigContainer::getMaxChargeCurrent_mA() const {
  uint16_t maxI = 100;
  loadMaxChrgI(maxI);
  return maxI;
}

/// @brief Gets current MPPT enable status from preferences
/// @return true if MPPT enabled in configuration
bool BoardConfigContainer::getMPPTEnabled() const {
  bool enabled;
  loadMpptEnabled(enabled);
  return enabled;
}

/// @brief Enables or disables MPPT
/// @param enableMPPT true to enable MPPT
/// @return true if successful
bool BoardConfigContainer::setMPPTEnable(bool enableMPPT) {
  // Save to preferences first
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  if (!prefs.putString(MPPTENABLEKEY, enableMPPT ? "1" : "0")) {
    return false;
  }
  
  // Set the hardware register
  if (!enableMPPT) {
    // Disable MPPT in hardware
    bq.setMPPTenable(false);
  } else {
    // Enable MPPT in hardware register
    bq.setMPPTenable(true);
  }
  
  return true;
}

/// @brief Gets current maximum charge voltage
/// @return Charge voltage limit in V
float BoardConfigContainer::getMaxChargeVoltage() const {
  return bq.getChargeLimitV();
};

/// @brief Sets battery type and reconfigures BQ accordingly
/// @param type Battery chemistry type
/// @return true if all configurations successful
bool BoardConfigContainer::setBatteryType(BatteryType type) {
  bool bqBaseConfigured = this->configureBaseBQ();
  bool bqConfigured = this->configureChemistry(type);
  cachedBatteryType = type;  // Update cache for static methods (updateBatterySOC, calculateTTL)

  // Invalidate SOC — voltage-to-SOC mapping changes with chemistry.
  // SOC will remain NA until next "Charging Done" sync or manual set.
  socStats.soc_valid = false;
  socStats.nominal_voltage = getNominalVoltage(type);

  // Restore correct IINDPM — configureBaseBQ() sets safe 2A default,
  // but USB must be capped to 500mA per USB 2.0 spec.
  if (usbInputActive && bqDriverInstance) {
    bqDriverInstance->setInputLimitA(IINDPM_USB_A);
    MESH_DEBUG_PRINTLN("USB active: IINDPM restored to %dmA after chemistry change", (int)(IINDPM_USB_A * 1000));
  } else {
    updateSolarIINDPM();
  }

  // === CRITICAL: Update INA228 low-voltage alert threshold when battery type changes ===
  if (ina228DriverInstance) {
    armLowVoltageAlert();
    delay(10);
  }
  
  // Store battery type in preferences
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  prefs.putString(BATTKEY, getBatteryTypeCommandString(type));
  
  // Safety: When switching to Li-Ion or LiFePO4, reset frost charge to NO_CHARGE
  // These chemistries should not be charged at low temperatures
  if (type == BatteryType::LIION_1S || type == BatteryType::LIFEPO4_1S) {
    setFrostChargeBehaviour(FrostChargeBehaviour::NO_CHARGE);
  }
  
  return bqBaseConfigured && bqConfigured;
}

/// @brief Sets frost charge behavior (JEITA cold region)
/// @param behaviour Charging behavior at low temperature
/// @return true if successful
bool BoardConfigContainer::setFrostChargeBehaviour(FrostChargeBehaviour behaviour) {
  switch (behaviour) {
  case BoardConfigContainer::FrostChargeBehaviour::NO_CHARGE:
    bq.setJeitaISetC(BQ25798_JEITA_ISETC_SUSPEND);
    break;
  case BoardConfigContainer::FrostChargeBehaviour::NO_REDUCE:
    bq.setJeitaISetC(BQ25798_JEITA_ISETC_UNCHANGED);
    break;
  case BoardConfigContainer::FrostChargeBehaviour::I_REDUCE_TO_40:
    bq.setJeitaISetC(BQ25798_JEITA_ISETC_40_PERCENT);
    break;
  case BoardConfigContainer::FrostChargeBehaviour::I_REDUCE_TO_20:
    bq.setJeitaISetC(BQ25798_JEITA_ISETC_20_PERCENT);
    break;
  }
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  prefs.putString(FROSTKEY, getFrostChargeBehaviourCommandString(behaviour));
  return true;
}

/// @brief Sets maximum charge current (ICHG) and recalculates solar IINDPM
/// @param maxChrgI Maximum charge current in mA
/// @return true if successful
/// @note Also calls updateSolarIINDPM() because IINDPM depends on ICHG.
bool BoardConfigContainer::setMaxChargeCurrent_mA(uint16_t maxChrgI) {
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  prefs.putInt(MAXCHARGECURRENTKEY, maxChrgI);

  bool ok = bq.setChargeLimitA(maxChrgI / 1000.0f);

  // Readback verification — detect silent I2C failures
  float readback = bq.getChargeLimitA();
  uint16_t readback_mA = (uint16_t)(readback * 1000.0f + 0.5f);

  MESH_DEBUG_PRINTLN("ICHG: set=%dmA, readback=%dmA, ok=%d", maxChrgI, readback_mA, ok);

  if (readback_mA != maxChrgI) {
    MESH_DEBUG_PRINTLN("WARNING: ICHG readback mismatch! Expected %d, got %d", maxChrgI, readback_mA);
  }

  // Recalculate solar IINDPM — it depends on charge current
  updateSolarIINDPM();

  return ok;
}

/// @brief Notify USB connection state change — adjusts IINDPM accordingly
/// @param connected true when USB VBUS detected, false when removed
/// @details USB: IINDPM = 500mA (USB 2.0 spec). No USB: IINDPM calculated from battery/charge config.
void BoardConfigContainer::setUsbConnected(bool connected) {
  if (usbInputActive == connected) return;  // No state change
  usbInputActive = connected;

  if (!bqDriverInstance) return;

  if (connected) {
    bqDriverInstance->setInputLimitA(IINDPM_USB_A);
    MESH_DEBUG_PRINTLN("USB connected: IINDPM = %dmA", (int)(IINDPM_USB_A * 1000));
  } else {
    updateSolarIINDPM();
  }
}

/// @brief Calculate IINDPM for solar input from battery chemistry and charge current
/// @return IINDPM in Amps, capped at IINDPM_MAX_A (JST limit), min 100mA (BQ25798 register min)
/// @details Power conservation: I_in = IINDPM_MARGIN × (V_charge × I_charge) / V_panel.
///          Prevents weak panels from POORSRC fault after PG qualification.
float BoardConfigContainer::calculateSolarIINDPM() {
  const BatteryProperties* props = getBatteryProperties(cachedBatteryType);
  if (!props || !props->charge_enable) {
    return IINDPM_MAX_A;  // Unknown chemistry: use safe max
  }

  // Read current imax from preferences
  uint16_t imax_mA = DEFAULT_MAX_CHARGE_CURRENT_MA;
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  char buffer[10];
  if (prefs.getString(MAXCHARGECURRENTKEY, buffer, sizeof(buffer), "") > 0) {
    int val = atoi(buffer);
    if (val > 0 && val <= 3000) {
      imax_mA = val;
    }
  }

  float i_charge_A = imax_mA / 1000.0f;
  float v_charge   = props->charge_voltage;

  // I_input = margin × (V_bat × I_bat) / V_panel
  float iindpm = IINDPM_MARGIN * (v_charge * i_charge_A) / IINDPM_PANEL_V;

  // Clamp to hardware limits
  if (iindpm > IINDPM_MAX_A) iindpm = IINDPM_MAX_A;
  if (iindpm < 0.1f)         iindpm = 0.1f;  // BQ25798 register minimum

  return iindpm;
}

/// @brief Apply calculated solar IINDPM to BQ25798 (skipped when USB active)
void BoardConfigContainer::updateSolarIINDPM() {
  if (usbInputActive || !bqDriverInstance) return;

  float iindpm = calculateSolarIINDPM();
  bqDriverInstance->setInputLimitA(iindpm);
  MESH_DEBUG_PRINTLN("Solar IINDPM = %dmA (Vchg=%.1fV, margin=%.1fx, Vpanel=%.0fV)",
                     (int)(iindpm * 1000),
                     getBatteryProperties(cachedBatteryType) ? getBatteryProperties(cachedBatteryType)->charge_voltage : 0.0f,
                     IINDPM_MARGIN, IINDPM_PANEL_V);
}

/// @brief Calculates 7-day moving average of MPPT enabled percentage
/// @return Percentage (0.0-100.0) of time MPPT was enabled over last 7 days
float BoardConfigContainer::getMpptEnabledPercentage7Day() const {
  // Return 0 if MPPT is disabled in config
  bool mpptEnabled;
  loadMpptEnabled(mpptEnabled);
  if (!mpptEnabled) {
    return 0.0f;
  }
  
  uint32_t totalMinutes = 0;
  uint32_t enabledMinutes = 0;
  uint32_t validHours = 0;
  
  // Count backwards through the circular buffer
  for (int i = 0; i < MPPT_STATS_HOURS; i++) {
    int index = (mpptStats.currentIndex - 1 - i + MPPT_STATS_HOURS) % MPPT_STATS_HOURS;
    
    // Skip entries that haven't been filled yet (timestamp == 0)
    if (mpptStats.hours[index].timestamp == 0) {
      continue;
    }
    
    validHours++;
    enabledMinutes += mpptStats.hours[index].mpptEnabledMinutes;
  }
  
  if (validHours == 0) {
    return 0.0f; // No data yet
  }
  
  totalMinutes = validHours * 60; // Each hour has 60 minutes
  
  return (enabledMinutes * 100.0f) / totalMinutes;
}

// ===== Battery SOC & Coulomb Counter Methods =====

/// @brief Get current State of Charge in percent
/// @return SOC in % (0-100)
float BoardConfigContainer::getStateOfCharge() const {
  return socStats.current_soc_percent;
}

/// @brief Get nominal voltage for battery chemistry type
/// @param type Battery chemistry type
/// @return Nominal voltage in V (used for mAh → mWh conversion)
float BoardConfigContainer::getNominalVoltage(BatteryType type) {
  const BatteryProperties* props = getBatteryProperties(type);
  return props ? props->nominal_voltage : 3.7f;
}

/// @brief Get battery capacity in mAh
/// @return Battery capacity (user-configured)
float BoardConfigContainer::getBatteryCapacity() const {
  return socStats.capacity_mah;
}

/// @brief Check if battery capacity was explicitly set via CLI
/// @return true if capacity was set in preferences, false if using default
bool BoardConfigContainer::isBatteryCapacitySet() const {
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[20];
  size_t len = prefs.getString(BATTERY_CAPACITY_KEY, buffer, sizeof(buffer), "");
  return (len > 0 && buffer[0] != '\0');
}

/// @brief Set battery capacity manually via CLI (converts to mWh internally)
/// @param capacity_mah Capacity in mAh (user input)
/// @return true if successful
bool BoardConfigContainer::setBatteryCapacity(float capacity_mah) {
  if (capacity_mah < 100.0f || capacity_mah > 100000.0f) {
    return false;  // Sanity check
  }
  
  // Store user-configured capacity in mAh
  socStats.capacity_mah = capacity_mah;
  
  // Get nominal voltage for current chemistry
  BatteryType batType = getBatteryType();
  float v_nominal = getNominalVoltage(batType);
  socStats.nominal_voltage = v_nominal;
  
  // Invalidate SOC until next "Charging Done" sync
  socStats.soc_valid = false;
  
  // Save to preferences
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%.1f", capacity_mah);
  prefs.putString(BATTERY_CAPACITY_KEY, buffer);
  
  MESH_DEBUG_PRINTLN("Battery capacity set to %.0f mAh @ %.1fV", 
                     capacity_mah, v_nominal);
  return true;
}

/// @brief Get Time To Live in hours
/// @details Based on the 7-day rolling average of daily net energy deficit (avg_7day_daily_net_mah),
///          calculated from hourly INA228 Coulomb-counter samples in a 168h ring buffer.
///          Formula: TTL = (current_soc% * capacity_mah) / abs(avg_7day_daily_net_mah) * 24h
/// @return Hours until battery empty (0 = not calculated, charging, or insufficient data)
uint16_t BoardConfigContainer::getTTL_Hours() const {
  return socStats.ttl_hours;
}

/// @brief Check if living on battery (net deficit)
/// @return true if using more than charging
bool BoardConfigContainer::isLivingOnBattery() const {
  return socStats.living_on_battery;
}

/// @brief Sync SOC to 100% after "Charging Done" event from BQ25798
/// @details Resets INA228 Coulomb Counter baseline and marks SOC as valid
void BoardConfigContainer::syncSOCToFull() {
  if (!ina228DriverInstance) {
    return;
  }
  
  // Reset INA228 Coulomb Counter (clears ENERGY and CHARGE registers)
  ina228DriverInstance->resetCoulombCounter();
  
  // Set baseline to 0 (we just reset the counter)
  socStats.ina228_baseline_mah = 0;
  socStats.last_soc_update_ms = millis();   // Reset time reference
  
  // Mark as fully charged
  socStats.current_soc_percent = 100.0f;
  socStats.soc_valid = true;
  
  // Update temperature derating factor immediately
  const BatteryProperties* props = getBatteryProperties(cachedBatteryType);
  uint32_t now_sync = millis();
  if (lastTempUpdateMs == 0 || (now_sync - lastTempUpdateMs) > 300000UL) {
    float bmeTemp = readBmeTemperature();
    if (bmeTemp > -100.0f && bmeTemp < 100.0f) {
      lastValidBatteryTemp = bmeTemp;
      lastTempUpdateMs = now_sync;
    }
  }
  socStats.temp_derating_factor = getTemperatureDerating(props, lastValidBatteryTemp);
  socStats.last_battery_temp_c = lastValidBatteryTemp;
  
  MESH_DEBUG_PRINTLN("SOC: Synced to 100%% (Charging Done) - INA228 baseline reset, d=%.2f",
                     socStats.temp_derating_factor);
}

/// @brief Manually set SOC to specific percentage (e.g. after reboot with known SOC)
/// @param soc_percent Desired SOC value (0-100)
/// @return true if successful, false if invalid parameters
bool BoardConfigContainer::setSOCManually(float soc_percent) {
  if (!ina228DriverInstance) {
    MESH_DEBUG_PRINTLN("SOC: Cannot set - INA228 not initialized");
    return false;
  }
  
  // Validate SOC range
  if (soc_percent < 0.0f || soc_percent > 100.0f) {
    MESH_DEBUG_PRINTLN("SOC: Invalid value %.1f%% (must be 0-100)", soc_percent);
    return false;
  }
  
  if (socStats.capacity_mah <= 0) {
    MESH_DEBUG_PRINTLN("SOC: Cannot set - battery capacity unknown");
    return false;
  }
  
  // Read current CHARGE register value
  float current_charge_mah = ina228DriverInstance->readCharge_mAh();
  
  // Calculate remaining capacity at desired SOC
  float remaining_mah = (soc_percent / 100.0f) * socStats.capacity_mah;
  
  // Calculate baseline: charge_mah = baseline + net_charge
  // We want: remaining_mah = capacity + net_charge = capacity + (charge - baseline)
  // Therefore: baseline = charge - (remaining - capacity)
  socStats.ina228_baseline_mah = current_charge_mah - (remaining_mah - socStats.capacity_mah);
  socStats.last_soc_update_ms = millis();   // Reset time reference
  
  // Set SOC and mark as valid
  socStats.current_soc_percent = soc_percent;
  socStats.soc_valid = true;
  
  // Update temperature derating factor immediately so telem/TTL are correct
  // without waiting for the next periodic updateBatterySOC() cycle.
  const BatteryProperties* props = getBatteryProperties(cachedBatteryType);
  uint32_t now_manual = millis();
  if (lastTempUpdateMs == 0 || (now_manual - lastTempUpdateMs) > 300000UL) {
    float bmeTemp = readBmeTemperature();
    if (bmeTemp > -100.0f && bmeTemp < 100.0f) {
      lastValidBatteryTemp = bmeTemp;
      lastTempUpdateMs = now_manual;
    }
  }
  socStats.temp_derating_factor = getTemperatureDerating(props, lastValidBatteryTemp);
  socStats.last_battery_temp_c = lastValidBatteryTemp;
  
  MESH_DEBUG_PRINTLN("SOC: Manually set to %.1f%% (CHARGE=%.1fmAh, Baseline=%.1fmAh, d=%.2f)",
                     soc_percent, current_charge_mah, socStats.ina228_baseline_mah,
                     socStats.temp_derating_factor);
  
  return true;
}

/// @brief Load battery capacity from preferences
/// @param capacity_mah Output parameter
/// @return true if loaded successfully
bool BoardConfigContainer::loadBatteryCapacity(float& capacity_mah) const {
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[20];
  if (prefs.getString(BATTERY_CAPACITY_KEY, buffer, sizeof(buffer), "") > 0) {
    if (buffer[0] != '\0') {
      capacity_mah = atof(buffer);
      return (capacity_mah > 0.0f);
    }
  }
  
  // Default capacity based on battery type (estimate)
  BatteryType type;
  if (loadBatType(type)) {
    switch (type) {
      case BatteryType::LTO_2S:
        capacity_mah = 2000.0f;  // Typical LTO capacity
        break;
      case BatteryType::LIFEPO4_1S:
        capacity_mah = 1500.0f;  // Typical LiFePO4 capacity
        break;
      case BatteryType::NAION_1S:
        capacity_mah = 2000.0f;  // Typical Na-Ion capacity
        break;
      case BatteryType::LIION_1S:
      default:
        capacity_mah = 2000.0f;  // Typical Li-Ion capacity
        break;
    }
  } else {
    capacity_mah = 2000.0f;  // Default fallback
  }
  
  return false;  // Not loaded from prefs
}

/// @brief Get INA228 driver instance
/// @return Pointer to INA228 driver or nullptr if not initialized
Ina228Driver* BoardConfigContainer::getIna228Driver() {
  return ina228DriverInstance;
}

// ===== NTC Temperature Calibration =====

/// @brief Load NTC temperature calibration offset from preferences
/// @param offset Output parameter (°C)
/// @return true if loaded successfully, false if using default (0.0)
bool BoardConfigContainer::loadTcCalOffset(float& offset) const {
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[20];
  if (prefs.getString(TCCAL_KEY, buffer, sizeof(buffer), "") > 0) {
    if (buffer[0] != '\0') {
      offset = atof(buffer);
      
      // Validate offset is in reasonable range (±20°C)
      if (offset >= -20.0f && offset <= 20.0f) {
        return true;
      }
    }
  }
  
  // Default: no offset
  offset = 0.0f;
  return false;
}

/// @brief Set NTC temperature calibration offset and save to preferences
/// @param offset_c Calibration offset in °C (-20 to +20)
/// @return true if saved successfully
bool BoardConfigContainer::setTcCalOffset(float offset_c) {
  // Clamp to reasonable range
  if (offset_c < -20.0f) offset_c = -20.0f;
  if (offset_c > 20.0f) offset_c = 20.0f;
  
  // Apply to runtime variable
  tcCalOffset = offset_c;
  
  // Save to preferences
  SimplePreferences prefs;
  prefs.begin(PREFS_NAMESPACE);
  
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%.2f", offset_c);
  
  if (prefs.putString(TCCAL_KEY, buffer)) {
    MESH_DEBUG_PRINTLN("TC calibration offset saved: %.2f °C", offset_c);
    return true;
  }
  
  return false;
}

/// @brief Get current NTC temperature calibration offset
/// @return Current offset in °C (0.0 = no calibration)
float BoardConfigContainer::getTcCalOffset() const {
  return tcCalOffset;
}



/// @brief Perform NTC temperature calibration using a reference temperature
/// Averages 5 NTC readings to reduce ADC noise, computes offset = reference - avg, stores it.
/// @param actual_temp_c Reference temperature in °C (e.g. from BME280)
/// @return Computed offset in °C, or -999.0 on error
float BoardConfigContainer::performTcCalibration(float actual_temp_c) {
  if (!bqDriverInstance) {
    return -999.0f;
  }
  
  // Temporarily remove any existing offset to get raw NTC readings
  float old_offset = tcCalOffset;
  tcCalOffset = 0.0f;
  
  // Average multiple NTC readings to reduce ADC noise
  const int NUM_SAMPLES = 5;
  const int SAMPLE_DELAY_MS = 200;
  float ntc_sum = 0.0f;
  int valid_count = 0;
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    if (i > 0) delay(SAMPLE_DELAY_MS);
    
    const Telemetry* bqData = bqDriverInstance->getTelemetryData(0);  // TC calibration: VBAT unknown, assume sufficient
    if (!bqData) continue;
    
    float raw = bqData->batterie.temperature;
    // Skip error codes
    if (raw <= -800.0f || raw >= 98.0f) continue;
    
    ntc_sum += raw;
    valid_count++;
  }
  
  if (valid_count < 3) {
    tcCalOffset = old_offset;  // Restore old offset
    MESH_DEBUG_PRINTLN("TC Cal: Only %d/%d valid NTC readings", valid_count, NUM_SAMPLES);
    return -999.0f;
  }
  
  float raw_ntc_avg = ntc_sum / valid_count;
  
  // Compute offset: calibrated = raw + offset  →  offset = reference - raw
  float new_offset = actual_temp_c - raw_ntc_avg;
  
  MESH_DEBUG_PRINTLN("TC Cal: ref=%.2f NTC_avg=%.2f (%d samples) offset=%.2f", 
                     actual_temp_c, raw_ntc_avg, valid_count, new_offset);
  
  // Store persistently
  if (!setTcCalOffset(new_offset)) {
    tcCalOffset = old_offset;  // Restore on failure
    return -999.0f;
  }
  
  return new_offset;
}

/// @brief Perform NTC temperature calibration using on-board BME280 as reference
/// Averages 5 BME280 readings, then delegates to performTcCalibration(float).
/// @param bme_temp_out Optional: receives the averaged BME temperature used for calibration
/// @return Computed offset in °C, or -999.0 on error
float BoardConfigContainer::performTcCalibration(float* bme_temp_out) {
  // Average multiple BME280 readings to reduce noise
  const int NUM_SAMPLES = 5;
  const int SAMPLE_DELAY_MS = 200;
  float bme_sum = 0.0f;
  int valid_count = 0;
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    float t = readBmeTemperature();
    if (t <= -900.0f) continue;
    bme_sum += t;
    valid_count++;
    if (i < NUM_SAMPLES - 1) delay(SAMPLE_DELAY_MS);
  }
  
  if (valid_count < 3) {
    MESH_DEBUG_PRINTLN("TC Cal: Only %d/%d valid BME readings", valid_count, NUM_SAMPLES);
    return -999.0f;
  }
  
  float bme_avg = bme_sum / valid_count;
  MESH_DEBUG_PRINTLN("TC Cal: BME avg=%.2f (%d samples)", bme_avg, valid_count);
  
  if (bme_temp_out) {
    *bme_temp_out = bme_avg;
  }
  
  return performTcCalibration(bme_avg);
}

/// @brief Read BME280 temperature directly via I2C (temporary instance, no core code changes)
/// @return Temperature in °C, or -999.0 if BME280 not available
float BoardConfigContainer::readBmeTemperature() {
#if ENV_INCLUDE_BME280
  Adafruit_BME280 bme;
  if (!bme.begin(0x76, &Wire)) {
    MESH_DEBUG_PRINTLN("TC Cal: BME280 not found at 0x76");
    return -999.0f;
  }
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::FILTER_OFF,
                  Adafruit_BME280::STANDBY_MS_1000);
  if (!bme.takeForcedMeasurement()) {
    MESH_DEBUG_PRINTLN("TC Cal: BME280 forced measurement failed");
    return -999.0f;
  }
  float temp = bme.readTemperature();
  MESH_DEBUG_PRINTLN("TC Cal: BME280 reads %.2f C", temp);
  return temp;
#else
  MESH_DEBUG_PRINTLN("TC Cal: BME280 not compiled in (ENV_INCLUDE_BME280=0)");
  return -999.0f;
#endif
}

/// @brief Arm INA228 low-voltage alert for current battery chemistry
/// @details Programs INA228 BUVL register with lowv_sleep_mv threshold.
///          Alert fires when VBAT drops below this level → ISR → volatile flag → tickPeriodic() → System Sleep.
///          Always active when battery type is configured (no CLI toggle).
///          BAT_UNKNOWN: alert disabled (threshold = 0).
void BoardConfigContainer::armLowVoltageAlert() {
  if (!ina228DriverInstance) {
    return;
  }
  
  BatteryType bat_type = getBatteryType();
  const BatteryProperties* props = getBatteryProperties(bat_type);
  uint16_t sleep_mv = props ? props->lowv_sleep_mv : 0;
  
  if (bat_type == BAT_UNKNOWN || sleep_mv == 0) {
    // No battery configured — disarm alert
    ina228DriverInstance->setUnderVoltageAlert(0);
    ina228DriverInstance->enableAlert(false, false, false);
    MESH_DEBUG_PRINTLN("INA228 Low-V Alert: DISABLED (BAT_UNKNOWN)");
    return;
  }
  
  bool buvl_ok = ina228DriverInstance->setUnderVoltageAlert(sleep_mv);
  ina228DriverInstance->enableAlert(true, false, true);  // active-LOW, LATCHED
  
  // Attach ISR on ALERT pin (active-LOW, falling edge)
  pinMode(INA_ALERT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INA_ALERT_PIN), lowVoltageAlertISR, FALLING);
  
  MESH_DEBUG_PRINTLN("INA228 Low-V Alert: ARMED @ %dmV (BUVL write %s)", sleep_mv, buvl_ok ? "OK" : "FAILED");
}

/// @brief Disarm INA228 low-voltage alert and detach ISR
void BoardConfigContainer::disarmLowVoltageAlert() {
  if (!ina228DriverInstance) {
    return;
  }
  
  detachInterrupt(digitalPinToInterrupt(INA_ALERT_PIN));
  ina228DriverInstance->setUnderVoltageAlert(0);
  ina228DriverInstance->enableAlert(false, false, false);
  lowVoltageAlertFired = false;
  MESH_DEBUG_PRINTLN("INA228 Low-V Alert: DISARMED");
}

/// @brief ISR for INA228 ALERT pin — sets flag checked in tickPeriodic()
/// @details Called on falling edge of INA228 ALERT (active-LOW, latched).
///          Sets volatile flag; tickPeriodic() checks it and initiates shutdown.
void BoardConfigContainer::lowVoltageAlertISR() {
  lowVoltageAlertFired = true;
}

/// @brief Get low-voltage sleep threshold (INA228 ALERT fires at this level)
/// @param type Battery chemistry type
/// @return Threshold in millivolts
uint16_t BoardConfigContainer::getLowVoltageSleepThreshold(BatteryType type) {
  const BatteryProperties* props = getBatteryProperties(type);
  return props ? props->lowv_sleep_mv : 2000;
}

/// @brief Get low-voltage wake threshold (RTC wake boots if VBAT >= this, 0% SOC marker)
/// @param type Battery chemistry type
/// @return Threshold in millivolts
uint16_t BoardConfigContainer::getLowVoltageWakeThreshold(BatteryType type) {
  const BatteryProperties* props = getBatteryProperties(type);
  return props ? props->lowv_wake_mv : 2200;
}

/// @brief Update battery SOC from INA228 Hardware Coulomb Counter
/// @details Uses INA228 CHARGE register (mAh) for accurate charge tracking
void BoardConfigContainer::updateBatterySOC() {
  if (!ina228DriverInstance) {
    return;
  }
  
  // Periodic SHUNT_CAL self-heal (~every 5 min via static counter)
  // If SHUNT_CAL got wiped (clone chip glitch, I2C error, etc.),
  // CURRENT and CHARGE registers read 0 forever → stats stay at 0.
  static uint8_t scal_check_counter = 0;
  if (++scal_check_counter >= 5) {  // Every 5th call = ~5 minutes (called every 60s)
    scal_check_counter = 0;
    ina228DriverInstance->validateAndRepairShuntCal();
  }
  
  // Read INA228 Hardware Coulomb Counter (mAh) - TWO'S COMPLEMENT, has correct sign!
  // Positive = charging (into battery), Negative = discharging (from battery)
  float charge_mah = ina228DriverInstance->readCharge_mAh();
  
  uint32_t now_ms = millis();
  socStats.last_soc_update_ms = now_ms;
  socStats.soc_update_count++;
  
  // Update current hour statistics (track charged/discharged charge in mAh)
  // This runs ALWAYS, independent of SOC validity
  static float last_charge_mah = 0.0f;
  static bool first_read = true;
  
  if (first_read) {
    // Initialize baseline on first read, don't count initial value as delta
    last_charge_mah = charge_mah;
    first_read = false;
  } else {
    float delta_mah = charge_mah - last_charge_mah;
    last_charge_mah = charge_mah;
    
    // Handle potential counter wrap or reset (ignore huge jumps > 10Ah)
    if (delta_mah > 10000.0f || delta_mah < -10000.0f) {
      MESH_DEBUG_PRINTLN("SOC: Large charge delta %.0fmAh - ignoring (counter reset?)", delta_mah);
    } else {
      // CHARGE register inverted in driver: positive delta = charging, negative delta = discharging
      if (delta_mah > 0.0f) {
        // Charging (positive delta)
        socStats.current_hour_charged_mah += delta_mah;
        socStats.current_hour_solar_mah += delta_mah;  // Assume solar (BQ tracks this)
      } else if (delta_mah < 0.0f) {
        // Discharging (negative delta)
        socStats.current_hour_discharged_mah += (-delta_mah);
      }
    }
  }
  socStats.last_charge_reading_mah = charge_mah;  // Always update for diagnostics
  
  // Check if BQ reports charging done → auto-sync
  if (bqDriverInstance) {
    bq25798_charging_status status = bqDriverInstance->getChargingStatus();
    if (status == BQ25798_CHARGER_STATE_DONE_CHARGING) {
      if (!socStats.soc_valid) {
        MESH_DEBUG_PRINTLN("SOC: First \"Charging Done\" detected - syncing to 100%%");
        syncSOCToFull();
        // Re-read CHARGE after counter reset to prevent false discharge spike
        last_charge_mah = ina228DriverInstance->readCharge_mAh();
      } else if (socStats.current_soc_percent < 99.0f) {
        MESH_DEBUG_PRINTLN("SOC: \"Charging Done\" detected - re-syncing to 100%%");
        syncSOCToFull();
        // Re-read CHARGE after counter reset to prevent false discharge spike
        last_charge_mah = ina228DriverInstance->readCharge_mAh();
      }
    }
  }
  
  // SOC calculation is only valid after first "Charging Done" sync via syncSOCToFull()
  if (!socStats.soc_valid) {
    return;  // Wait for first sync
  }
  
  // Net charge since last baseline reset (using CHARGE register in mAh)
  // Driver inverted: positive = charged into battery, negative = discharged from battery
  float net_charge_mah = charge_mah - socStats.ina228_baseline_mah;
  
  // Remaining capacity = Initial capacity + net charge (positive=charged adds, negative=discharged subtracts)
  float remaining_mah = socStats.capacity_mah + net_charge_mah;
  
  // Temperature derating: calculate factor for TTL and display purposes.
  // The derating factor is NOT applied to SOC% — SOC% is purely Coulomb-based
  // (remaining_mah / capacity_mah) and represents the actual stored charge.
  // Derating only affects TTL calculation (extractable capacity) and is shown
  // separately in CLI output as "derated SOC%".
  //
  // Temperature source priority:
  //   1. NTC via BQ25798 TS ADC (cached in lastValidBatteryTemp by getTelemetryData())
  //   2. BME280 fallback — used when NTC has not provided a reading for >5 minutes.
  //      This covers Na-Ion and LTO setups where ts_ignore=true and no NTC is connected.
  //      BME280 measures PCB/ambient temperature, not battery temperature directly,
  //      but is a reasonable proxy (typically within ±3°C in a sealed enclosure).
  const BatteryProperties* props = getBatteryProperties(cachedBatteryType);
  
  // BME280 fallback: if NTC hasn't updated for >5 min, try BME280
  uint32_t now_derating = millis();
  if (lastTempUpdateMs == 0 || (now_derating - lastTempUpdateMs) > 300000UL) {
    float bmeTemp = readBmeTemperature();
    if (bmeTemp > -100.0f && bmeTemp < 100.0f) {
      lastValidBatteryTemp = bmeTemp;
      lastTempUpdateMs = now_derating;
    }
    // If BME280 also fails, lastValidBatteryTemp keeps its previous value (default 25°C = no derating)
  }
  
  float derating = getTemperatureDerating(props, lastValidBatteryTemp);
  socStats.temp_derating_factor = derating;
  socStats.last_battery_temp_c = lastValidBatteryTemp;
  
  // Calculate SOC percentage — purely Coulomb-based, NO temperature derating
  if (socStats.capacity_mah > 0) {
    socStats.current_soc_percent = (remaining_mah / socStats.capacity_mah) * 100.0f;
    
    // Clamp to 0-100%
    if (socStats.current_soc_percent > 100.0f) socStats.current_soc_percent = 100.0f;
    if (socStats.current_soc_percent < 0.0f) socStats.current_soc_percent = 0.0f;
  }
}

/// @brief Update daily balance statistics (mAh-based)
/// @brief Update hourly battery statistics and advance rolling window
uint32_t BoardConfigContainer::getRTCTimestamp() {
  return getRTCTime();
}

void BoardConfigContainer::updateHourlyStats() {
  uint32_t currentTime = getRTCTime();
  
  // Calculate hour boundary (align to full hours)
  uint32_t currentHour = (currentTime / 3600) * 3600;  // Truncate to hour boundary
  
  // Check if hour has changed
  if (socStats.lastHourUpdateTime == 0) {
    // First run - initialize
    socStats.lastHourUpdateTime = currentHour;
    MESH_DEBUG_PRINTLN("SOC: Hourly stats initialized at timestamp %u", currentHour);
    return;
  }
  
  uint32_t lastHour = (socStats.lastHourUpdateTime / 3600) * 3600;
  
  if (currentHour > lastHour) {
    // Hour boundary crossed - save current hour stats
    MESH_DEBUG_PRINTLN("SOC: Hour changed (%u -> %u) - saving stats: C:%.1f D:%.1f S:%.1f mAh", 
                       lastHour, currentHour,
                       socStats.current_hour_charged_mah,
                       socStats.current_hour_discharged_mah,
                       socStats.current_hour_solar_mah);
    
    // Move to next hour slot in circular buffer
    uint8_t nextIndex = (socStats.currentIndex + 1) % HOURLY_STATS_HOURS;
    
    // Save completed hour's stats
    HourlyBatteryStats& completedHour = socStats.hours[socStats.currentIndex];
    completedHour.timestamp = lastHour;
    completedHour.charged_mah = socStats.current_hour_charged_mah;
    completedHour.discharged_mah = socStats.current_hour_discharged_mah;
    completedHour.solar_mah = socStats.current_hour_solar_mah;
    
    // Reset accumulators for new hour
    socStats.currentIndex = nextIndex;
    socStats.current_hour_charged_mah = 0.0f;
    socStats.current_hour_discharged_mah = 0.0f;
    socStats.current_hour_solar_mah = 0.0f;
    socStats.lastHourUpdateTime = currentHour;
    
    // Recalculate rolling window statistics (24h and 3-day averages)
    calculateRollingStats();
  }
}

/// @brief Calculate 24h and 3-day rolling averages from hourly buffer
void BoardConfigContainer::calculateRollingStats() {
  // Calculate last 24 hours net balance
  float sum_24h_charged = 0.0f;
  float sum_24h_discharged = 0.0f;
  float sum_24h_solar = 0.0f;
  int valid_hours_24h = 0;
  
  // Sum up last 24 hours (most recent 24 entries)
  for (int i = 0; i < 24 && i < HOURLY_STATS_HOURS; i++) {
    int idx = (socStats.currentIndex - 1 - i + HOURLY_STATS_HOURS) % HOURLY_STATS_HOURS;
    if (socStats.hours[idx].timestamp != 0) {
      sum_24h_charged += socStats.hours[idx].charged_mah;
      sum_24h_discharged += socStats.hours[idx].discharged_mah;
      sum_24h_solar += socStats.hours[idx].solar_mah;
      valid_hours_24h++;
    }
  }
  
  // Last 24h net: solar - discharged (positive = surplus, negative = deficit)
  socStats.last_24h_net_mah = sum_24h_solar - sum_24h_discharged;
  socStats.last_24h_charged_mah = sum_24h_charged;
  socStats.last_24h_discharged_mah = sum_24h_discharged;
  socStats.living_on_battery = (socStats.last_24h_net_mah < 0.0f);
  
  // Calculate 3-day average daily net (72 hours)
  float sum_72h_charged = 0.0f;
  float sum_72h_discharged = 0.0f;
  float sum_72h_solar = 0.0f;
  int valid_hours_72h = 0;
  
  for (int i = 0; i < 72 && i < HOURLY_STATS_HOURS; i++) {
    int idx = (socStats.currentIndex - 1 - i + HOURLY_STATS_HOURS) % HOURLY_STATS_HOURS;
    if (socStats.hours[idx].timestamp != 0) {
      sum_72h_charged += socStats.hours[idx].charged_mah;
      sum_72h_discharged += socStats.hours[idx].discharged_mah;
      sum_72h_solar += socStats.hours[idx].solar_mah;
      valid_hours_72h++;
    }
  }
  
  // Average daily net over 3 days (divide 72h sum by 3)
  if (valid_hours_72h >= 24) {  // Need at least 24h of data
    float net_72h = sum_72h_solar - sum_72h_discharged;
    socStats.avg_3day_daily_net_mah = net_72h / 3.0f;  // Divide by 3 days
    socStats.avg_3day_daily_charged_mah = sum_72h_charged / 3.0f;
    socStats.avg_3day_daily_discharged_mah = sum_72h_discharged / 3.0f;
  } else {
    socStats.avg_3day_daily_net_mah = 0.0f;
    socStats.avg_3day_daily_charged_mah = 0.0f;
    socStats.avg_3day_daily_discharged_mah = 0.0f;
  }
  
  // Calculate 7-day average daily net (168 hours)
  float sum_168h_charged = 0.0f;
  float sum_168h_discharged = 0.0f;
  float sum_168h_solar = 0.0f;
  int valid_hours_168h = 0;
  
  for (int i = 0; i < 168 && i < HOURLY_STATS_HOURS; i++) {
    int idx = (socStats.currentIndex - 1 - i + HOURLY_STATS_HOURS) % HOURLY_STATS_HOURS;
    if (socStats.hours[idx].timestamp != 0) {
      sum_168h_charged += socStats.hours[idx].charged_mah;
      sum_168h_discharged += socStats.hours[idx].discharged_mah;
      sum_168h_solar += socStats.hours[idx].solar_mah;
      valid_hours_168h++;
    }
  }
  
  // Average daily net over 7 days (divide 168h sum by 7)
  if (valid_hours_168h >= 24) {  // Need at least 24h of data
    float net_168h = sum_168h_solar - sum_168h_discharged;
    socStats.avg_7day_daily_net_mah = net_168h / 7.0f;  // Divide by 7 days
    socStats.avg_7day_daily_charged_mah = sum_168h_charged / 7.0f;
    socStats.avg_7day_daily_discharged_mah = sum_168h_discharged / 7.0f;
  } else {
    socStats.avg_7day_daily_net_mah = 0.0f;
    socStats.avg_7day_daily_charged_mah = 0.0f;
    socStats.avg_7day_daily_discharged_mah = 0.0f;
  }
  
  MESH_DEBUG_PRINTLN("SOC: Rolling stats - 24h net: %+.1fmAh, 3d avg: %+.1fmAh/day, 7d avg: %+.1fmAh/day",
                     socStats.last_24h_net_mah, socStats.avg_3day_daily_net_mah, socStats.avg_7day_daily_net_mah);
  
  // Calculate TTL
  calculateTTL();
}

/// @brief Calculate Time To Live (hours until battery empty)
/// @details TTL is based on the **7-day rolling average** of daily net energy consumption
///          (avg_7day_daily_net_mah). This average is computed from a 168-hour (7-day)
///          ring buffer of hourly INA228 Coulomb-counter measurements (charged/discharged/solar mAh).
///
///          Data flow:
///          1. INA228 hardware Coulomb counter measures charge flow continuously (24-bit ADC)
///          2. updateHourlyStats() samples the counter every hour, storing per-hour deltas
///             (charged_mah, discharged_mah, solar_mah) in hours[168] ring buffer
///          3. calculateRollingStats() sums the last 168 hours and divides by 7 to get
///             avg_7day_daily_net_mah (= solar - discharged per day)
///          4. This method extrapolates: remaining_mah / deficit_per_day * 24 = TTL hours
///
///          Preconditions for TTL > 0:
///          - living_on_battery == true (24h net is negative, i.e. energy deficit)
///          - avg_7day_daily_net_mah < 0 (7-day average shows net discharge)
///          - capacity_mah > 0 (battery capacity is known)
///          - At least 24 hours of valid hourly data exist in the ring buffer
///
///          When the device is solar-powered with energy surplus (net >= 0),
///          TTL is 0 and callers interpret this as "infinite" via living_on_battery flag.
void BoardConfigContainer::calculateTTL() {
  if (!socStats.living_on_battery || socStats.avg_7day_daily_net_mah >= 0) {
    socStats.ttl_hours = 0;  // Not draining or charging
    return;
  }
  
  if (socStats.capacity_mah <= 0) {
    socStats.ttl_hours = 0;  // Capacity unknown
    return;
  }
  
  // Trapped Charge model: cold temperatures "lock" the bottom of the discharge
  // curve — the cell shuts down (OCV near cutoff + TX-peak IR-drop) while charge
  // is still physically stored.  trapped_mah is the unusable floor.
  //   trapped_mah  = capacity × (1 − f(T))     e.g. 8000 × 0.17 = 1360 mAh at −10 °C
  //   extractable  = max(0, remaining − trapped)
  // This is more realistic than proportional scaling (remaining × f) because
  // capacity loss at cold is not uniform — it steals from the bottom.
  float remaining_mah = (socStats.current_soc_percent / 100.0f) * socStats.capacity_mah;
  float trapped_mah = socStats.capacity_mah * (1.0f - socStats.temp_derating_factor);
  float extractable_mah = remaining_mah - trapped_mah;
  if (extractable_mah < 0.0f) extractable_mah = 0.0f;
  
  // Daily deficit (negative value)
  float deficit_per_day = -socStats.avg_7day_daily_net_mah;
  
  if (deficit_per_day <= 0) {
    socStats.ttl_hours = 0;
    return;
  }
  
  // Days until empty (based on extractable capacity)
  float days_remaining = extractable_mah / deficit_per_day;
  
  // Convert to hours
  socStats.ttl_hours = (uint16_t)(days_remaining * 24.0f);
  
  MESH_DEBUG_PRINTLN("TTL: %.1f days (%.0f mAh stored, %.0f trapped, %.0f extractable @d=%.2f, -%.0f mAh/day)",
                     days_remaining, remaining_mah, trapped_mah, extractable_mah, socStats.temp_derating_factor, deficit_per_day);
}

// ===== Tick-based Periodic Dispatch =====

/// @brief Called from InheroMr2Board::tick() — dispatches all periodic I2C work
/// @details millis()-based scheduling in the main loop context.
///          Also checks the ISR-set lowVoltageAlertFired flag for immediate shutdown.
void BoardConfigContainer::tickPeriodic() {
  // First-call init: clear MPPT stats
  if (!tickInitialized) {
    memset(&mpptStats, 0, sizeof(mpptStats));
    tickInitialized = true;
  }

  // Check low-voltage alert flag (set by INA228 ALERT ISR)
  if (lowVoltageAlertFired) {
    MESH_DEBUG_PRINTLN("PWRMGT: Low-voltage alert fired — initiating System Sleep");
    blinkRed(1, 100, 100, leds_enabled);
    blinkRed(3, 300, 300, leds_enabled);

    NRF_POWER->GPREGRET2 |= GPREGRET2_LOW_VOLTAGE_SLEEP;
    board.initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE);
    // Never returns
  }

  uint32_t now = millis();

  // Every ~60s: MPPT cycle (solar charging control)
  if (now - lastMpptMs >= SOLAR_MPPT_INTERVAL_MS) {
    lastMpptMs = now;
    runMpptCycle();
  }

  // Every ~60s: SOC update from Coulomb Counter
  if (now - lastSocMs >= 60000UL) {
    lastSocMs = now;
    updateBatterySOC();
  }

  // Every ~60 min: hourly statistics
  if (now - lastHourlyMs >= 3600000UL) {
    lastHourlyMs = now;
    MESH_DEBUG_PRINTLN("SOC: 60 minutes elapsed - updating hourly stats");
    updateHourlyStats();
  }
}

// ===== Helper Functions =====

/// @brief Trim whitespace from string
char* BoardConfigContainer::trim(char* str) {
  char* end;

  while (isspace((unsigned char)*str))
    str++;

  if (*str == 0) {
    return str;
  }

  end = str + strlen(str) - 1;

  while (end > str && isspace((unsigned char)*end))
    end--;

  *(end + 1) = 0;

  return str;
}

/// @brief Convert command string to battery type enum
BoardConfigContainer::BatteryType BoardConfigContainer::getBatteryTypeFromCommandString(const char* cmdStr) {
  for (const auto& entry : bat_map) {
    if (entry.command_string == nullptr) break;
    if (strcmp(entry.command_string, cmdStr) == 0) {
      return entry.type;
    }
  }
  return BatteryType::BAT_UNKNOWN;
}

/// @brief Get battery properties for a given battery type
/// @param type Battery type
/// @return Pointer to BatteryProperties structure, or nullptr if not found
const BoardConfigContainer::BatteryProperties* BoardConfigContainer::getBatteryProperties(BatteryType type) {
  for (const auto& props : battery_properties) {
    if (props.type == type) {
      return &props;
    }
  }
  return nullptr;  // Should never happen if battery_properties is complete
}

/// @brief Temperature derating factor for a battery chemistry
/// @details At cold temperatures, the internal resistance of a battery increases,
///          reducing the extractable capacity — even though the stored charge
///          (measured by the coulomb counter) remains unchanged. This function
///          returns a factor 0.0–1.0 that scales the nominal capacity to reflect
///          the actually available (extractable) capacity.
///
///          Model:  f(T) = 1.0                                  for T >= T_ref
///                  f(T) = max(f_min, 1.0 - k * (T_ref - T))   for T <  T_ref
///
///          The linear model is a conservative approximation sufficient for SOC/TTL
///          display purposes. Real curves are slightly concave (capacity drops faster
///          at extreme cold), but the f_min clamp prevents unrealistic values.
///
/// @param props Battery properties (contains k, f_min, T_ref per chemistry)
/// @param temp_c Current battery temperature in °C
/// @return Derating factor (1.0 = full capacity, e.g. 0.75 = 75% extractable)
float BoardConfigContainer::getTemperatureDerating(const BatteryProperties* props, float temp_c) {
  if (!props) return 1.0f;
  if (temp_c >= props->temp_ref_c) return 1.0f;

  float delta = props->temp_ref_c - temp_c;
  float factor = 1.0f - props->temp_derating_k * delta;
  if (factor < props->temp_derating_min) factor = props->temp_derating_min;
  return factor;
}

/// @brief Convert battery type enum to command string
const char* BoardConfigContainer::getBatteryTypeCommandString(BatteryType type) {
  for (const auto& entry : bat_map) {
    if (entry.command_string == nullptr) break;
    if (entry.type == type) {
      return entry.command_string;
    }
  }
  return "unknown";
}

/// @brief Convert frost charge behaviour enum to command string
const char* BoardConfigContainer::getFrostChargeBehaviourCommandString(FrostChargeBehaviour type) {
  for (const auto& entry : frostchargebehaviour_map) {
    if (entry.command_string == nullptr) break;
    if (entry.type == type) {
      return entry.command_string;
    }
  }
  return "unknown";
}

/// @brief Convert command string to frost charge behaviour enum
BoardConfigContainer::FrostChargeBehaviour BoardConfigContainer::getFrostChargeBehaviourFromCommandString(const char* cmdStr) {
  for (const auto& entry : frostchargebehaviour_map) {
    if (entry.command_string == nullptr) break;
    if (strcmp(entry.command_string, cmdStr) == 0) {
      return entry.type;
    }
  }
  return FrostChargeBehaviour::REDUCE_UNKNOWN;
}

/// @brief Get available frost charge behaviour option strings
const char* BoardConfigContainer::getAvailableFrostChargeBehaviourOptions() {
  static char buffer[64];

  if (buffer[0] != '\0') return buffer;

  buffer[0] = '\0';

  for (const auto& entry : frostchargebehaviour_map) {
    if (entry.command_string == nullptr) break;

    size_t space_needed = strlen(buffer) + 1 + strlen(entry.command_string) + 1;

    if (space_needed >= sizeof(buffer)) {
      break;
    }

    if (buffer[0] != '\0') {
      strcat(buffer, "|");
    }
    strcat(buffer, entry.command_string);
  }

  return buffer;
}

/// @brief Get available battery type option strings
const char* BoardConfigContainer::getAvailableBatOptions() {
  static char buffer[64];

  if (buffer[0] != '\0') return buffer;

  buffer[0] = '\0';

  for (const auto& entry : bat_map) {
    if (entry.command_string == nullptr) break;

    size_t space_needed = strlen(buffer) + 1 + strlen(entry.command_string) + 1;

    if (space_needed >= sizeof(buffer)) {
      break;
    }

    if (buffer[0] != '\0') {
      strcat(buffer, "|");
    }
    strcat(buffer, entry.command_string);
  }

  return buffer;
}
