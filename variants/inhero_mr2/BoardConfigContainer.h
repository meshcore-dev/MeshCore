/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "lib/BqDriver.h"
#include "lib/Ina228Driver.h"

#include <Arduino.h>

#define SOLAR_MPPT_INTERVAL_MS (1 * 60 * 1000)  // 1 minute

#define MPPT_STATS_HOURS 168  // 7 days

typedef struct {
  uint8_t mpptEnabledMinutes;     // 0–60
  uint32_t timestamp;             // unix seconds
  uint32_t harvestedEnergy_mWh;
} MpptHourlyStats;

typedef struct {
  MpptHourlyStats hours[MPPT_STATS_HOURS];
  uint8_t currentIndex;
  uint32_t lastUpdateTime;        // unix seconds, or millis if RTC unavailable
  uint16_t currentHourMinutes;
  bool usingRTC;
  uint32_t currentHourEnergy_mWh;
  int32_t lastPower_mW;
} MpptStatistics;

// Battery SOC: mAh-based, uses INA228 hardware coulomb counter (CHARGE register)
#define HOURLY_STATS_HOURS 168  // 7 days

typedef struct {
  uint32_t timestamp;             // start of hour, unix seconds
  float charged_mah;
  float discharged_mah;
  float solar_mah;
} HourlyBatteryStats;

typedef struct {
  // Battery configuration
  float capacity_mah;
  float nominal_voltage;

  // SOC tracking via INA228 CHARGE register
  float current_soc_percent;      // 0–100
  bool soc_valid;                 // true after first "Charging Done" sync
  float ina228_baseline_mah;      // INA228 CHARGE at last 100% sync

  uint32_t last_soc_update_ms;

  // 168-hour rolling buffer
  HourlyBatteryStats hours[HOURLY_STATS_HOURS];
  uint8_t currentIndex;
  uint32_t lastHourUpdateTime;

  // Current hour accumulators
  float current_hour_charged_mah;
  float current_hour_discharged_mah;
  float current_hour_solar_mah;
  float last_charge_reading_mah;  // last INA228 CHARGE for delta calc

  // Rolling window stats (computed from hourly buffer)
  float last_24h_net_mah;
  float last_24h_charged_mah;
  float last_24h_discharged_mah;
  float avg_3day_daily_net_mah;
  float avg_3day_daily_charged_mah;
  float avg_3day_daily_discharged_mah;
  float avg_7day_daily_net_mah;
  float avg_7day_daily_charged_mah;
  float avg_7day_daily_discharged_mah;
  // TTL: hours until battery empty (0 = not calculated). Based on 7-day rolling
  // avg of daily net deficit from INA228 coulomb-counter samples.
  uint16_t ttl_hours;
  bool living_on_battery;         // net deficit over last 24h
  uint16_t soc_update_count;
  float temp_derating_factor;     // 0.0–1.0
  float last_battery_temp_c;
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
    float charge_voltage;
    float nominal_voltage;
    uint16_t lowv_sleep_mv;     // INA228 ALERT → System Sleep
    uint16_t lowv_wake_mv;      // 0% SOC marker, RTC wake decision
    bool charge_enable;
    bool ts_ignore;             // disables JEITA temperature monitoring
    // Capacity derating at cold temps. Calibrated for ~0.01C avg / ~0.05C TX
    // peak loads on 2–8 Ah cells (much milder than datasheet 0.2C–0.5C values).
    //   f(T) = 1.0                                 for T >= temp_ref_c
    //   f(T) = max(temp_derating_min, 1 - k*(Tref - T))  for T < temp_ref_c
    float temp_derating_k;
    float temp_derating_min;
    float temp_ref_c;
  } BatteryProperties;

  static inline constexpr BatteryProperties battery_properties[] = {
    // Type         ChgV  NomV  SleepMv WakeMv  ChgEn  TsIgn   k       min   Tref
    { BAT_UNKNOWN,  0.0f, 0.0f, 2000,  2200,   false, true,   0.000f, 1.00f, 25.0f },
    { LTO_2S,       5.4f, 4.6f, 3900,  4100,   true,  true,   0.002f, 0.88f, 25.0f },
    { LIFEPO4_1S,   3.5f, 3.2f, 2700,  2900,   true,  false,  0.006f, 0.70f, 25.0f },
    { LIION_1S,     4.1f, 3.7f, 3100,  3300,   true,  false,  0.005f, 0.75f, 25.0f },
    { NAION_1S,     3.9f, 3.1f, 2500,  2700,   true,  true,   0.003f, 0.85f, 25.0f }
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

  // Defaults for newly flashed boards
  static constexpr BatteryType DEFAULT_BATTERY_TYPE = BAT_UNKNOWN;
  static constexpr FrostChargeBehaviour DEFAULT_FROST_BEHAVIOUR = NO_CHARGE;
  static constexpr uint16_t DEFAULT_MAX_CHARGE_CURRENT_MA = 200;
  static constexpr bool DEFAULT_MPPT_ENABLED = false;

  // IINDPM = 1.2 × (V_charge × I_charge) / V_panel_assumed.
  // Prevents weak panels from tripping POORSRC after PG qualification.
  static constexpr float IINDPM_MAX_A   = 2.0f;  // JST connector limit
  static constexpr float IINDPM_USB_A   = 0.5f;  // USB 2.0 max
  static constexpr float IINDPM_PANEL_V = 4.0f;
  static constexpr float IINDPM_MARGIN  = 1.2f;

  // If PG=0 but VBUS >= this, toggle HIZ to force input re-qualification.
  static constexpr uint16_t PG_STUCK_VBUS_THRESHOLD_MV = 4500;

  static BatteryType getBatteryTypeFromCommandString(const char* cmdStr);
  static char* trim(char* str);
  static const char* getBatteryTypeCommandString(BatteryType type);
  static const char* getFrostChargeBehaviourCommandString(FrostChargeBehaviour type);
  static FrostChargeBehaviour getFrostChargeBehaviourFromCommandString(const char* cmdStr);
  static const char* getAvailableFrostChargeBehaviourOptions();
  static const char* getAvailableBatOptions();
  static const BatteryProperties* getBatteryProperties(BatteryType type);

  // Returns 0.0–1.0; reduces SOC% and TTL at cold temps. Coulomb counter unaffected.
  static float getTemperatureDerating(const BatteryProperties* props, float temp_c);

  static void heartbeatTask(void* pvParameters);

  // Re-enable MPPT if BQ disabled it (when PG=1).
  static void checkAndFixSolarLogic();

  static bool loadMpptEnabled(bool& enabled);
  void tickPeriodic();              // periodic I2C work (MPPT, SOC, hourly stats)
  static void stopBackgroundTasks();

  bool setBatteryType(BatteryType type);

  BatteryType getBatteryType() const;

  bool setFrostChargeBehaviour(FrostChargeBehaviour behaviour);
  FrostChargeBehaviour getFrostChargeBehaviour() const;

  bool setMaxChargeCurrent_mA(uint16_t maxChrgI);
  uint16_t getMaxChargeCurrent_mA() const;

  // Caps IINDPM to 500mA when USB is the input source.
  static void setUsbConnected(bool connected);
  static bool isUsbConnected() { return usbInputActive; }
  static float calculateSolarIINDPM();
  static void updateSolarIINDPM();

  bool getMPPTEnabled() const;
  bool setMPPTEnable(bool enableMPPT);

  float getMaxChargeVoltage() const;

  bool begin();

  // INA228 for VBAT/IBAT, BQ25798 for solar.
  const Telemetry* getTelemetryData();

  const char* getChargeCurrentAsStr();
  void getChargerInfo(char* buffer, uint32_t bufferSize);
  void getBqDiagnostics(char* buffer, uint32_t bufferSize);

  // "INA:OK BQ:OK RTC:OK BME:OK". RTC probe writes/reads user-RAM to catch
  // zombie chips that ACK but don't persist.
  void getSelfTest(char* buffer, uint32_t bufferSize);

  // Address ACK + user-RAM write/readback verify (bytes 0x1F, 0x20 are scratch).
  static bool probeRtc();

  float getMpptEnabledPercentage7Day() const;

  // Battery SOC & coulomb counter
  float getStateOfCharge() const;
  float getBatteryCapacity() const;
  bool setBatteryCapacity(float capacity_mah);
  bool isBatteryCapacitySet() const;
  uint16_t getTTL_Hours() const;
  bool isLivingOnBattery() const;
  // Sync SOC to 100% after "Charging Done".
  static void syncSOCToFull();
  static bool setSOCManually(float soc_percent);
  const BatterySOCStats* getSOCStats() const { return &socStats; }
  const MpptStatistics* getMpptStats() const { return &mpptStats; }
  static void updateBatterySOC();
  static uint32_t getRTCTimestamp();

  static float getNominalVoltage(BatteryType type);
  void setLowVoltageRecovery() { lowVoltageRecovery = true; }
  Ina228Driver* getIna228Driver();

  // NTC calibration via BME280 reference
  bool setTcCalOffset(float offset_c);
  float getTcCalOffset() const;
  float performTcCalibration(float* bme_temp_out = nullptr);
  static float readBmeTemperature();

  // INA228 ALERT on P1.02 (Rev 1.1)
  void armLowVoltageAlert();
  static void disarmLowVoltageAlert();
  static void lowVoltageAlertISR();

  static uint16_t getLowVoltageSleepThreshold(BatteryType type);
  static uint16_t getLowVoltageWakeThreshold(BatteryType type);

  // 600s timeout; nRF52 WDT cannot truly be disabled.
  static void setupWatchdog();
  static void feedWatchdog();
  static void disableWatchdog();

  bool setLEDsEnabled(bool enabled);
  bool getLEDsEnabled() const;

private:
  static BqDriver* bqDriverInstance;
  static Ina228Driver* ina228DriverInstance;
  static TaskHandle_t heartbeatTaskHandle;
  static volatile bool lowVoltageAlertFired;  // INA228 ALERT fired, checked in tickPeriodic

  // Tick scheduling (millis-based, overflow-safe)
  uint32_t lastMpptMs = 0;
  uint32_t lastSocMs = 0;
  uint32_t lastHourlyMs = 0;       // Last updateHourlyStats() execution
  bool tickInitialized = false;    // First-call init flag for MPPT stats

  void runMpptCycle();             // Single MPPT cycle
  static MpptStatistics mpptStats; // MPPT statistics data
  static BatterySOCStats socStats; // Battery SOC statistics
  // Cached battery type for static methods (set by begin()/setBatteryType())
  static BatteryType cachedBatteryType;

  bool bqInitialized = false;
  bool ina228Initialized = false;
  bool lowVoltageRecovery = false;  // Set in begin() if booting from low-voltage sleep (GPREGRET2)
  static bool leds_enabled;  // Heartbeat and BQ stat LED control (static for ISR access)
  static bool usbInputActive; // True when USB VBUS detected — caps IINDPM to 500mA
  static float tcCalOffset;   // NTC temperature calibration offset in °C (0.0 = no calibration)
  // Last valid battery temperature in °C, updated by getTelemetryData() or
  // BME280 fallback (default 25.0 = no derating)
  static float lastValidBatteryTemp;
  static uint32_t lastTempUpdateMs;   // millis() of last valid temperature update (0 = never updated)

  // Refresh socStats.temp_derating_factor and last_battery_temp_c.
  // Falls back to BME280 if NTC has not updated for >5 min.
  static void refreshTempDerating();

  bool configureBaseBQ();
  bool configureChemistry(BatteryType type);
  float performTcCalibration(float actual_temp_c); // Internal: calibrate NTC given reference temp (called by BME auto-cal)
  static constexpr const char* PREFS_NAMESPACE = "inheromr2";
  static constexpr const char* BATTKEY = "batType";
  static constexpr const char* FROSTKEY = "frost";
  static constexpr const char* MAXCHARGECURRENTKEY = "maxChrg";
  static constexpr const char* MPPTENABLEKEY = "mpptEn";
  static constexpr const char* LEDSKEY = "leds_en";
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
  static void updateHourlyStats();   // Update hourly statistics (called every 60 minutes)
  static void calculateRollingStats(); // Calculate 24h and 3-day averages from rolling buffer
  static void calculateTTL();    // Calculate TTL from 7-day avg net deficit and remaining SOC capacity
};