#pragma once

#include <Arduino.h>
#include <string>
#include <vector>

#define SHUTDOWN_REASON_LOW_VOLTAGE 0x4C
#define SHUTDOWN_REASON_BOOT_PROTECT 0x42

struct PowerMgtConfig {
  uint8_t lpcomp_ain_channel;
  uint8_t lpcomp_refsel;
  uint16_t voltage_bootlock;
};

namespace startup_test {
extern std::vector<std::string> events;
extern bool permitBoot;
struct SystemOff { uint8_t reason; };
}

class NRF52Board {
protected:
  bool checkBootVoltage(const PowerMgtConfig*) {
    startup_test::events.push_back("voltage");
    if (!startup_test::permitBoot) initiateShutdown(SHUTDOWN_REASON_BOOT_PROTECT);
    return true;
  }
  virtual void initiateShutdown(uint8_t reason) { enterSystemOff(reason); }
  void configureVoltageWake(uint8_t, uint8_t) {}
  void enterSystemOff(uint8_t reason) {
    startup_test::events.push_back("off");
    throw startup_test::SystemOff{reason};
  }
public:
  explicit NRF52Board(const char* = nullptr) {}
  virtual uint16_t getBattMilliVolts() = 0;
  virtual const char* getManufacturerName() const = 0;
};

class NRF52BoardDCDC : virtual public NRF52Board {
public:
  void begin() {}
};
