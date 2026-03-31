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

// Battery chemistry types
enum BatteryChemistry {
  CHEM_LIION = 0,  // Li-ion/LiPo: 3.0V - 4.2V
  CHEM_LFP = 1,    // Lithium Iron Phosphate: 2.5V - 3.65V
  CHEM_LTO = 2     // Lithium Titanate Oxide: 1.8V - 2.85V
};

// Boards provide this struct with their hardware-specific settings and callbacks.
struct PowerMgtConfig {
  // LPCOMP wake configuration (for voltage recovery from SYSTEMOFF)
  uint8_t lpcomp_ain_channel;       // AIN0-7 for voltage sensing pin
  uint8_t lpcomp_refsel_liion;      // Li-ion wake REFSEL (0-6=N/8, 8-15=N/16); 0xFF = VBUS-only
  uint8_t lpcomp_refsel_lfp;        // LFP wake REFSEL; 0xFF = VBUS-only
  uint8_t lpcomp_refsel_lto;        // LTO wake REFSEL; 0xFF = VBUS-only

  // Boot protection voltage thresholds per chemistry (millivolts)
  uint16_t voltage_bootlock_liion;  // Li-ion minimum safe voltage
  uint16_t voltage_bootlock_lfp;    // LFP minimum safe voltage
  uint16_t voltage_bootlock_lto;    // LTO minimum safe voltage
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
  BatteryChemistry battery_chem;      // Configured battery chemistry (read from com_prefs)

  bool checkBootVoltage(const PowerMgtConfig* config);
  void enterSystemOff(uint8_t reason);
  void configureVoltageWake(uint8_t ain_channel, uint8_t refsel);
  virtual void initiateShutdown(uint8_t reason);
  uint16_t getThresholdForChemistry(BatteryChemistry chem, const PowerMgtConfig* config);
  uint8_t getRefselForChemistry(BatteryChemistry chem, const PowerMgtConfig* config);
  const char* getChemistryString(BatteryChemistry chem);
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

#ifdef NRF52_POWER_MANAGEMENT
  bool isExternalPowered() override;
  uint16_t getBootVoltage() override { return boot_voltage_mv; }
  virtual uint32_t getResetReason() const override { return reset_reason; }
  uint8_t getShutdownReason() const override { return shutdown_reason; }
  const char* getResetReasonString(uint32_t reason) override;
  const char* getShutdownReasonString(uint8_t reason) override;
#endif
};

/*
 * The NRF52 has an internal DC/DC regulator that allows increased efficiency
 * compared to the LDO regulator. For being able to use it, the module/board
 * needs to have the required inductors and and capacitors populated. If the
 * hardware requirements are met, this subclass can be used to enable the DC/DC
 * regulator.
 */
class NRF52BoardDCDC : virtual public NRF52Board {
public:
  NRF52BoardDCDC() {}
  virtual void begin() override;
};
#endif