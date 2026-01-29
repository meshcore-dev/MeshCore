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

  bool checkBootVoltage(const PowerMgtConfig* config);
  void enterSystemOff(uint8_t reason);
  void configureVoltageWake(uint8_t ain_channel, uint8_t refsel);
  virtual void initiateShutdown(uint8_t reason);
#endif

public:
  NRF52Board(char *otaname) : ota_name(otaname) {}
  virtual void begin();
  virtual uint8_t getStartupReason() const override { return startup_reason; }
  virtual float getMCUTemperature() override;
  virtual void reboot() override { NVIC_SystemReset(); }
  virtual bool startOTAUpdate(const char *id, char reply[]) override;

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