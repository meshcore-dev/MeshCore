#pragma once

// Standalone 1-Wire bus driver for EnvironmentSensorManager.
//
// Covers every DallasTemperature-supported temperature family on one
// bus pin: DS18B20 (0x28), DS18S20 (0x10), DS1822 (0x22), DS1825 /
// MAX31850 (0x3B), DS28EA00 (0x42). Each detected device is exposed
// as its own telemetry sub-channel, so an air-and-water deployment
// with two probes on the same pin "just works".
//
// Portability contract: this file contains no board-specific pins or
// #ifdefs. The bus pin comes from the per-board build flag
// TELEM_ONEWIRE_PIN, and the whole module is compiled out when
// ENV_INCLUDE_ONEWIRE is not set.

#if ENV_INCLUDE_ONEWIRE

#include <Arduino.h>
#include <CayenneLPP.h>
#include <OneWire.h>
#include <DallasTemperature.h>

class OneWireSensor {
public:
  // Upper bound on devices enumerated from a single bus. Kept under
  // EnvironmentSensorManager::MAX_ACTIVE_SENSORS so a fully populated
  // bus cannot overflow the active-sensor table on its own.
  static const uint8_t MAX_DEVICES = 8;

  explicit OneWireSensor(uint8_t pin);

  // Probes the bus for a 1-Wire presence pulse — the direct analog of
  // the I2C ACK check the manager's address scan relies on. With no
  // pulse, returns 0 and DallasTemperature is never engaged; this
  // preserves the manager's "never touch absent hardware" invariant.
  // Otherwise enumerates the bus and returns the number of detected
  // temperature-family devices (clamped to MAX_DEVICES).
  uint8_t begin();

  // Reads device `index` and writes one temperature channel.
  void query(uint8_t channel, uint8_t index, CayenneLPP& lpp);

private:
  OneWire           _bus;
  DallasTemperature _dallas;
  uint8_t           _count = 0;
  DeviceAddress     _addresses[MAX_DEVICES];
};

#endif // ENV_INCLUDE_ONEWIRE
