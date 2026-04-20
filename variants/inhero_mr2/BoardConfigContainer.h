/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * Board Configuration Container for Inhero MR-2
 */
#pragma once
#include "lib/BqDriver.h"
#include "lib/Ina228Driver.h"

#include <Arduino.h>

// Solar MPPT polling interval (no interrupt — pure polling)
#define SOLAR_MPPT_INTERVAL_MS (1 * 60 * 1000)  // 1 minute

// MPPT Statistics tracking for 7-day moving average
#define MPPT_STATS_HOURS 168  // 7 days * 24 hours

typedef struct {
  uint8_t mpptEnabledMinutes;  ///< Minutes MPPT was enabled in this hour (0-60)
  uint32_t timestamp;          ///< Unix timestamp (seconds) for this hour
  uint32_t harvestedEnergy_mWh; ///< Harvested solar energy (mWh) during this hour
} MpptHourlyStats;

typedef struct {
  MpptHourlyStats hours[MPPT_STATS_HOURS]; ///< Rolling buffer of hourly stats
  uint8_t currentIndex;                    ///< Current position in circular buffer
  uint32_t lastUpdateTime;                 ///< Last update time (Unix seconds or millis if RTC unavailable)
  uint16_t currentHourMinutes;             ///< Accumulated minutes for current hour
  bool usingRTC;                           ///< True if using RTC, false if using millis fallback
  uint32_t currentHourEnergy_mWh;          ///< Accumulated energy for current hour (mWh)
  int32_t lastPower_mW;                    ///< Last measured power for energy calculation
} MpptStatistics;

// Battery SOC Tracking - mAh-based using INA228 Hardware Coulomb Counter (CHARGE register)
#define HOURLY_STATS_HOURS 168  // 7 days * 24 hours = 168 hours

// Hourly battery statistics for rolling window
typedef struct {
  uint32_t timestamp;           ///< Unix timestamp (start of hour, seconds)
  float charged_mah;            ///< Charge added this hour (mAh)
  float discharged_mah;         ///< Charge removed this hour (mAh)
  float solar_mah;              ///< Solar charge contribution this hour (mAh)
} HourlyBatteryStats;

typedef struct {
  // Battery configuration
  float capacity_mah;          ///< Total battery capacity in mAh
  float nominal_voltage;       ///< Nominal voltage for current chemistry (V)
  
  // SOC tracking using INA228 hardware counter (CHARGE register in mAh)
  float current_soc_percent;   ///< Current State of Charge in % (0-100)
  bool soc_valid;              ///< True after first "Charging Done" sync
  float ina228_baseline_mah;   ///< INA228 CHARGE reading at last 100% sync (mAh)

  uint32_t last_soc_update_ms;  ///< millis() of last updateBatterySOC() call (for offset dt)
  
  // Hourly statistics (168-hour rolling buffer for 7 days)
  HourlyBatteryStats hours[HOURLY_STATS_HOURS];
  uint8_t currentIndex;
  uint32_t lastHourUpdateTime;  ///< Last hour boundary timestamp
  
  // Current hour accumulators (reset every hour)
  float current_hour_charged_mah;
  float current_hour_discharged_mah;
  float current_hour_solar_mah;
  float last_charge_reading_mah;  ///< Last INA228 CHARGE reading for delta calculation
  
  // Rolling window statistics (calculated from hourly buffer)
  float last_24h_net_mah;        ///< Net balance over last 24 hours
  float last_24h_charged_mah;    ///< Total charged over last 24 hours
  float last_24h_discharged_mah; ///< Total discharged over last 24 hours
  float avg_3day_daily_net_mah;  ///< Average daily net over last 3 days (72h)
  float avg_3day_daily_charged_mah;    ///< Average daily charged over last 3 days
  float avg_3day_daily_discharged_mah; ///< Average daily discharged over last 3 days
  float avg_7day_daily_net_mah;  ///< Average daily net over last 7 days (168h)
  float avg_7day_daily_charged_mah;    ///< Average daily charged over last 7 days
  float avg_7day_daily_discharged_mah; ///< Average daily discharged over last 7 days
  uint16_t ttl_hours;            ///< Time To Live - hours until battery empty (0 = not calculated).
                                 ///< Based on 7-day rolling avg of daily net deficit (avg_7day_daily_net_mah)
                                 ///< from hourly INA228 Coulomb-counter samples in 168h ring buffer.
  bool living_on_battery;        ///< True if net deficit over last 24h
  uint16_t soc_update_count;     ///< Debug: number of updateBatterySOC() calls since boot
  float temp_derating_factor;    ///< Current temperature derating factor (0.0–1.0, 1.0 = no derating)
  float last_battery_temp_c;     ///< Last valid battery temperature used for derating (°C)
} BatterySOCStats;

class BoardConfigContainer {

public:
  enum BatteryType : uint8_t { BAT_UNKNOWN = 0, LTO_2S = 1, LIFEPO4_1S = 2, LIION_1S = 3, NAION_1S = 4 };
  typedef struct {
    const char* command_string;
    BatteryType type;
  } BatteryMapping;

  // Battery type properties
  typedef struct {
    BatteryType type;
    float charge_voltage;       // Max charge voltage in V
    float nominal_voltage;      // Nominal voltage for energy calculations
    uint16_t lowv_sleep_mv;     // Low-voltage sleep threshold (INA228 ALERT triggers System Sleep) in mV
    uint16_t lowv_wake_mv;      // Low-voltage wake threshold (0% SOC marker, RTC wake decision) in mV
    bool charge_enable;         // Enable/disable charging (false for BAT_UNKNOWN)
    bool ts_ignore;             // Ignore TS/NTC temperature monitoring (disables JEITA)
    // Temperature derating — models reduced extractable capacity at cold temperatures.
    // At our typical load (~8 mA average, ~100 mA TX peaks), capacity loss is much smaller
    // than datasheet values measured at 0.2C–0.5C. The real risk at cold temperatures is
    // transient voltage dips during TX peaks (100 mA × increased R_internal) that could
    // trigger the INA228 low-voltage alert and cause premature System Sleep.
    // Values are calibrated for ~0.01C average / ~0.05C peak discharge rates.
    //   f(T) = 1.0                                      for T >= temp_ref_c
    //   f(T) = max(temp_derating_min, 1 - k*(Tref - T))  for T <  temp_ref_c
    float temp_derating_k;      // Capacity loss per °C below temp_ref_c (e.g. 0.005 = 0.5%/°C)
    float temp_derating_min;    // Minimum derating factor (clamp, e.g. 0.75 = 75% at extreme cold)
    float temp_ref_c;           // Reference temperature in °C where capacity = 100% (typically 25.0)
  } BatteryProperties;

  // Battery properties lookup table
  // All battery-specific thresholds in one central location
  // Temperature derating columns: k = capacity loss per °C below Tref, min = floor factor, Tref = reference temp
  //   Values calibrated for low C-rate discharge (~0.01C avg, ~0.05C TX peaks).
  //   Standard datasheet values (0.2C–0.5C) would be 2–3× more aggressive but are unrealistic
  //   for our ~8 mA average / ~100 mA peak loads on 2–8 Ah cells.
  static inline constexpr BatteryProperties battery_properties[] = {
    // Type         ChgV  NomV  SleepMv WakeMv  ChgEn  TsIgn   k       min   Tref
    { BAT_UNKNOWN,  0.0f, 0.0f, 2000,  2200,   false, true,   0.000f, 1.00f, 25.0f }, // No derating
    { LTO_2S,       5.4f, 4.6f, 3900,  4100,   true,  true,   0.002f, 0.88f, 25.0f }, // LTO: best cold performance
    { LIFEPO4_1S,   3.5f, 3.2f, 2700,  2900,   true,  false,  0.006f, 0.70f, 25.0f }, // LiFePO4: flat plateau helps at low C
    { LIION_1S,     4.1f, 3.7f, 3100,  3300,   true,  false,  0.005f, 0.75f, 25.0f }, // Li-Ion NMC/NCA: moderate
    { NAION_1S,     3.9f, 3.1f, 2500,  2700,   true,  true,   0.003f, 0.85f, 25.0f }  // Na-Ion: good cold tolerance
  };

  static inline constexpr BatteryMapping bat_map[] = { { "lto2s", LTO_2S },
                                                       { "lifepo1s", LIFEPO4_1S },
                                                       { "liion1s", LIION_1S },
                                                       { "naion1s", NAION_1S },
                                                       { "none", BAT_UNKNOWN },
                                                       { nullptr, BAT_UNKNOWN } };

  enum FrostChargeBehaviour : uint8_t {
    NO_CHARGE = 4,
    I_REDUCE_TO_20 = 3,
    I_REDUCE_TO_40 = 2,
    NO_REDUCE = 1,
    REDUCE_UNKNOWN = 0
  };
  typedef struct {
    const char* command_string;
    FrostChargeBehaviour type;
  } FrostChargeBehaviourMapping;

  static inline constexpr FrostChargeBehaviourMapping frostchargebehaviour_map[] = {
    { "0%", NO_CHARGE },
    { "20%", I_REDUCE_TO_20 },
    { "40%", I_REDUCE_TO_40 },
    { "100%", NO_REDUCE },
    { nullptr, REDUCE_UNKNOWN }
  };

  // Default values for newly flashed boards
  static constexpr BatteryType DEFAULT_BATTERY_TYPE = BAT_UNKNOWN;  // Safe: low thresholds, user must configure
  static constexpr FrostChargeBehaviour DEFAULT_FROST_BEHAVIOUR = NO_CHARGE;
  static constexpr uint16_t DEFAULT_MAX_CHARGE_CURRENT_MA = 200;
  static constexpr bool DEFAULT_MPPT_ENABLED = false;

  // IINDPM (input current limit) — calculated from battery voltage and charge current.
  // Formula: IINDPM = 1.2 × (V_charge × I_charge) / V_panel_assumed
  // This prevents weak panels from tripping POORSRC after PG qualification.
  static constexpr float IINDPM_MAX_A   = 2.0f;  // JST connector limit on PCB (hard cap)
  static constexpr float IINDPM_USB_A   = 0.5f;  // USB 2.0 spec maximum
  static constexpr float IINDPM_PANEL_V = 4.0f;  // Conservative panel voltage for IINDPM calc
  static constexpr float IINDPM_MARGIN  = 1.2f;  // Headroom for converter efficiency

  // PG-Stuck recovery: VBUS threshold above which a panel is assumed present.
  // If PG=0 but VBUS >= this value, toggle HIZ to force input re-qualification.
  static constexpr uint16_t PG_STUCK_VBUS_THRESHOLD_MV = 4500;

  static BatteryType getBatteryTypeFromCommandString(const char* cmdStr);
  static char* trim(char* str);
  static const char* getBatteryTypeCommandString(BatteryType type);
  static const char* getFrostChargeBehaviourCommandString(FrostChargeBehaviour type);
  static FrostChargeBehaviour getFrostChargeBehaviourFromCommandString(const char* cmdStr);
  static const char* getAvailableFrostChargeBehaviourOptions();
  static const char* getAvailableBatOptions();
  static const BatteryProperties* getBatteryProperties(BatteryType type);

  /// @brief Get temperature derating factor for a battery chemistry
  /// @details Returns 0.0–1.0 (1.0 = full capacity at or above reference temperature).
  ///          The derating reduces SOC% and TTL to reflect reduced extractable energy at cold temps.
  ///          The coulomb counter itself is NOT affected — it always records true charge flow.
  /// @param props Battery properties (contains derating coefficients)
  /// @param temp_c Current battery temperature in °C
  /// @return Derating factor (1.0 = no derating, <1.0 = reduced available capacity)
  static float getTemperatureDerating(const BatteryProperties* props, float temp_c);
  
  // Solar Power Management Functions
  // These functions work together to handle stuck PGOOD conditions and MPPT recovery:
  
  static void heartbeatTask(void* pvParameters);

  /// Re-enable MPPT if BQ disabled it (when PG=1).
  static void checkAndFixSolarLogic();

  static bool loadMpptEnabled(bool& enabled);
  void tickPeriodic();              ///< Called from tick() — dispatches all periodic I2C work (MPPT, SOC, hourly stats)
  static void stopBackgroundTasks(); ///< Stop heartbeat task and disarm alerts before OTA

  bool setBatteryType(BatteryType type);

  BatteryType getBatteryType() const;

  bool setFrostChargeBehaviour(FrostChargeBehaviour behaviour);
  FrostChargeBehaviour getFrostChargeBehaviour() const;

  bool setMaxChargeCurrent_mA(uint16_t maxChrgI);
  uint16_t getMaxChargeCurrent_mA() const;

  static void setUsbConnected(bool connected);  ///< Notify USB state change — caps IINDPM to 500mA when USB is source
  static bool isUsbConnected() { return usbInputActive; }
  static float calculateSolarIINDPM();  ///< Calculate IINDPM from battery voltage and charge current
  static void updateSolarIINDPM();      ///< Apply calculated solar IINDPM (no-op when USB active)

  bool getMPPTEnabled() const;
  bool setMPPTEnable(bool enableMPPT);

  float getMaxChargeVoltage() const;

  bool begin();

  const Telemetry* getTelemetryData(); ///< Get combined telemetry (INA228 for VBAT/IBAT, BQ25798 for Solar)

  const char* getChargeCurrentAsStr();
  void getChargerInfo(char* buffer, uint32_t bufferSize);
  void getBqDiagnostics(char* buffer, uint32_t bufferSize);

  /// @brief Probe all I2C devices on the board and report status.
  /// @details Tests INA228 (0x40), BQ25798 (0x6B), RV-3028 (0x52, with
  ///          user-RAM write/readback to catch \"zombie\" chips), BME280 (0x76).
  ///          Format: \"INA:OK BQ:OK RTC:OK BME:OK\" or \"INA:OK BQ:NACK RTC:WR_FAIL BME:OK\".
  /// @param buffer Output buffer
  /// @param bufferSize Output buffer size
  void getSelfTest(char* buffer, uint32_t bufferSize);

  /// @brief Probe RV-3028 RTC: address ACK + user-RAM write/readback verify.
  /// @details Catches RTCs that ACK on the bus but do not persist register
  ///          writes. User-RAM bytes (0x1F, 0x20) per RV-3028 datasheet are
  ///          scratch and safe to overwrite; original byte is restored.
  /// @return true if RTC ACKs and persists a write, false otherwise.
  static bool probeRtc();
  
  // MPPT Statistics methods
  float getMpptEnabledPercentage7Day() const;  ///< Get 7-day moving average of MPPT enabled %
  
  // Battery SOC & Coulomb Counter methods
  float getStateOfCharge() const;              ///< Get current SOC in % (0-100)
  float getBatteryCapacity() const;            ///< Get battery capacity in mAh
  bool setBatteryCapacity(float capacity_mah); ///< Set battery capacity manually via CLI (converts to mWh internally)
  bool isBatteryCapacitySet() const;           ///< Check if battery capacity was explicitly set (vs default)
  uint16_t getTTL_Hours() const;               ///< Get Time To Live in hours (0 = not calculated)
  bool isLivingOnBattery() const;              ///< True if net deficit over last 24h
  static void syncSOCToFull();                 ///< Sync SOC to 100% after "Charging Done" (resets INA228 baseline)
  static bool setSOCManually(float soc_percent); ///< Manually set SOC to specific value (e.g. after reboot)
  const BatterySOCStats* getSOCStats() const { return &socStats; } ///< Get SOC stats for CLI
  const MpptStatistics* getMpptStats() const { return &mpptStats; } ///< Get MPPT stats for CLI
  static void updateBatterySOC();              ///< Update SOC from INA228 Coulomb Counter
  static uint32_t getRTCTimestamp();            ///< Get current RTC time (for diagnostics)

  static float getNominalVoltage(BatteryType type); ///< Get nominal voltage for chemistry type
  void setLowVoltageRecovery() { lowVoltageRecovery = true; } ///< Mark as low-voltage recovery boot
  Ina228Driver* getIna228Driver();             ///< Get INA228 driver instance
  
  // NTC Temperature Calibration methods
  bool setTcCalOffset(float offset_c);           ///< Store temperature calibration offset in °C (persistent)
  float getTcCalOffset() const;                  ///< Get current temperature calibration offset
  float performTcCalibration(float* bme_temp_out = nullptr); ///< Calibrate NTC using BME280 as auto-reference
  static float readBmeTemperature();               ///< Read BME280 temperature directly via I2C
  
  // Low-voltage alert methods (Rev 1.1 — INA228 ALERT on P1.02)
  void armLowVoltageAlert();    ///< Arm INA228 BUVL alert at lowv_sleep_mv (called on battery config)
  static void disarmLowVoltageAlert(); ///< Disarm INA228 BUVL alert and detach ISR
  static void lowVoltageAlertISR(); ///< ISR for INA228 ALERT pin — sets flag (checked in tickPeriodic)
  
  // Voltage threshold helpers (chemistry-specific)
  static uint16_t getLowVoltageSleepThreshold(BatteryType type);   ///< Get sleep threshold (INA228 ALERT)
  static uint16_t getLowVoltageWakeThreshold(BatteryType type);    ///< Get wake threshold (0% SOC marker)
  
  // Watchdog methods
  static void setupWatchdog();   ///< Initialize and start hardware watchdog (600s timeout)
  static void feedWatchdog();    ///< Feed the watchdog to prevent reset
  static void disableWatchdog(); ///< Disable watchdog before OTA (cannot truly disable nRF52 WDT)
  
  // LED control methods
  bool setLEDsEnabled(bool enabled); ///< Enable/disable heartbeat LED and BQ stat LED (persistent)
  bool getLEDsEnabled() const;       ///< Get current LED enable state

private:
  static BqDriver* bqDriverInstance; ///< Singleton reference for static methods
  static Ina228Driver* ina228DriverInstance; ///< Singleton reference for INA228
  static TaskHandle_t heartbeatTaskHandle; ///< Handle for heartbeat task
  static volatile bool lowVoltageAlertFired; ///< ISR flag: INA228 ALERT fired (checked in tickPeriodic)

  // Tick-based scheduling state (millis()-based, overflow-safe)
  uint32_t lastMpptMs = 0;         ///< Last runMpptCycle() execution
  uint32_t lastSocMs = 0;          ///< Last updateBatterySOC() execution
  uint32_t lastHourlyMs = 0;       ///< Last updateHourlyStats() execution
  bool tickInitialized = false;    ///< First-call init flag for MPPT stats

  void runMpptCycle();             ///< Single MPPT cycle
  static MpptStatistics mpptStats; ///< MPPT statistics data
  static BatterySOCStats socStats; ///< Battery SOC statistics
  static BatteryType cachedBatteryType; ///< Cached battery type for static methods (set by begin()/setBatteryType())
  
  bool BQ_INITIALIZED = false;
  bool INA228_INITIALIZED = false;
  bool lowVoltageRecovery = false;  ///< Set in begin() if booting from low-voltage sleep (GPREGRET2)
  static bool leds_enabled;  // Heartbeat and BQ stat LED control (static for ISR access)
  static bool usbInputActive; // True when USB VBUS detected — caps IINDPM to 500mA
  static float tcCalOffset;   // NTC temperature calibration offset in °C (0.0 = no calibration)
  static float lastValidBatteryTemp;  // Last valid battery temperature in °C (updated by getTelemetryData() or BME280 fallback, default 25.0 = no derating)
  static uint32_t lastTempUpdateMs;   // millis() of last valid temperature update (0 = never updated)

  bool configureBaseBQ();
  bool configureChemistry(BatteryType type);
  float performTcCalibration(float actual_temp_c); ///< Internal: calibrate NTC given reference temp (called by BME auto-cal)
  static constexpr const char* PREFS_NAMESPACE = "inheromr2";
  static constexpr const char* BATTKEY = "batType";
  static constexpr const char* FROSTKEY = "frost";
  static constexpr const char* MAXCHARGECURRENTKEY = "maxChrg";
  static constexpr const char* MPPTENABLEKEY = "mpptEn";
  static constexpr const char* BATTERY_CAPACITY_KEY = "batCap";
  static constexpr const char* TCCAL_KEY = "tcCal";              // NTC temperature calibration offset


  bool loadBatType(BatteryType& type) const;
  bool loadFrost(FrostChargeBehaviour& behaviour) const;
  bool loadMaxChrgI(uint16_t& maxCharge_mA) const;
  bool loadBatteryCapacity(float& capacity_mah) const;
  bool loadTcCalOffset(float& offset) const;  // NTC temperature calibration

  
  // MPPT Statistics helper
  static void updateMpptStats();

  // Battery SOC helpers
  static void updateHourlyStats();   ///< Update hourly statistics (called every 60 minutes)
  static void calculateRollingStats(); ///< Calculate 24h and 3-day averages from rolling buffer
  static void calculateTTL();    ///< Calculate TTL from 7-day avg net deficit and remaining SOC capacity
};