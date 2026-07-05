#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#if defined(NRF52_PLATFORM)

#ifdef NRF52_POWER_MANAGEMENT
// Shutdown Reason Codes (stored in GPREGRET before SYSTEMOFF)
#define SHUTDOWN_REASON_NONE          0x00
#define SHUTDOWN_REASON_LOW_VOLTAGE   0x4C  // 'L' - Runtime low voltage threshold
#define SHUTDOWN_REASON_USER          0x55  // 'U' - User requested powerOff()
#define SHUTDOWN_REASON_BOOT_PROTECT  0x42  // 'B' - Boot voltage protection

// Boards provide this struct with their hardware-specific settings and callbacks.
struct PowerMgtConfig {
  // LPCOMP wake configuration (for voltage recovery from SYSTEMOFF)
  uint8_t lpcomp_ain_channel;       // AIN0-7 for voltage sensing pin
  uint8_t lpcomp_refsel;            // REFSEL value: 0-6=1/8..7/8, 7=ARef, 8-15=1/16..15/16

  // Boot protection voltage threshold (millivolts)
  // Set to 0 to disable boot protection
  uint16_t voltage_bootlock;

  // Capability flags describe what this board can prove from its sense wiring.
  // They prevent VBUS loss from being treated as proof that a BAT sense node is valid.
  bool battery_voltage_sense_valid;
  bool lpcomp_voltage_wake_valid;
  bool vbus_wake_valid;

  // Battery voltage interpretation thresholds. Readings below the present
  // threshold are treated as absent/floating, not as a discharged battery.
  uint16_t battery_min_present_mv;

  // Broad plausibility limits for source-state confidence reporting.
  uint16_t battery_min_plausible_mv;
  uint16_t battery_max_plausible_mv;

  // Optional nRF52 power-fail warning threshold for regulated VDD.
  // Set to 0 to disable runtime power-fail shutdown. Threshold values are
  // the nRF52 POWER_POFCON_THRESHOLD_* enum values, for example
  // POWER_POFCON_THRESHOLD_V28 for 2.8 V.
  uint8_t power_fail_vdd_threshold;

  // If true, runtime power-fail shutdown arms VBUS detect as the SYSTEMOFF
  // wake source. This is useful for boards powered by a battery on VUSB where
  // BAT sense is not available, but it only applies after a deliberate
  // firmware shutdown before uncontrolled brownout.
  bool power_fail_vbus_wake;
};
#endif

class NRF52Board : public mesh::MainBoard {
#ifdef NRF52_POWER_MANAGEMENT
  void initPowerMgr();
#endif

protected:
  uint8_t startup_reason;
  char *ota_name;

#ifdef NRF52_POWER_MANAGEMENT
  uint32_t reset_reason;              // RESETREAS register value
  uint8_t shutdown_reason;            // GPREGRET value (why we entered last SYSTEMOFF)
  uint16_t boot_voltage_mv;           // Battery voltage at boot (millivolts)
  const PowerMgtConfig* active_power_config = nullptr;

  bool checkBootVoltage(const PowerMgtConfig* config);
  void enterSystemOff(uint8_t reason);
  void configureVoltageWake(uint8_t ain_channel, uint8_t refsel);
  void configureVoltageWake(const PowerMgtConfig* config);
  void configureVbusWake();
  void configurePowerFailShutdown(const PowerMgtConfig* config);
  void disablePowerFailShutdown();
  virtual void initiateShutdown(uint8_t reason);
  bool isBatteryVoltagePlausible(uint16_t millivolts, const PowerMgtConfig* config) const;
#endif

public:
  NRF52Board(char *otaname) : ota_name(otaname) {}
  virtual void begin();
  virtual uint8_t getStartupReason() const override { return startup_reason; }
  virtual float getMCUTemperature() override;
  virtual void reboot() override { NVIC_SystemReset(); }
  virtual bool getBootloaderVersion(char* version, size_t max_len) override;
  virtual bool startOTAUpdate(const char *id, char reply[]) override;
  virtual void sleep(uint32_t secs) override;
  bool isExternalPowered() override;

#ifdef NRF52_POWER_MANAGEMENT
  uint16_t getBootVoltage() override { return boot_voltage_mv; }
  bool isBootVoltageValid() override;
  const char* getPowerSourceState() override;
  virtual uint32_t getResetReason() const override { return reset_reason; }
  uint8_t getShutdownReason() const override { return shutdown_reason; }
  const char* getResetReasonString(uint32_t reason) override;
  const char* getShutdownReasonString(uint8_t reason) override;
#endif
};

/*
 * The NRF52 has an internal DC/DC regulator that allows increased efficiency
 * compared to the LDO regulator. For being able to use it, the module/board
 * needs to have the required inductors and capacitors populated. If the
 * hardware requirements are met, this subclass can be used to enable the DC/DC
 * regulator.
 */
class NRF52BoardDCDC : virtual public NRF52Board {
public:
  NRF52BoardDCDC() {}
  virtual void begin() override;
};
#endif
