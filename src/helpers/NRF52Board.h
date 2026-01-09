#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#if defined(NRF52_PLATFORM)

class NRF52Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  virtual void begin();
  virtual uint8_t getStartupReason() const override { return startup_reason; }
  virtual float getMCUTemperature() override;
  virtual void reboot() override { NVIC_SystemReset(); }
  virtual void enterLightSleep(uint32_t secs);
  virtual void sleep(uint32_t secs) override;
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
  virtual void begin() override;
};

class NRF52BoardOTA : virtual public NRF52Board {
private:
  char *ota_name;

public:
  NRF52BoardOTA(char *name) : ota_name(name) {}
  virtual bool startOTAUpdate(const char *id, char reply[]) override;
};
#endif