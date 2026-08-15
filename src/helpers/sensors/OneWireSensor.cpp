#include "OneWireSensor.h"

#if ENV_INCLUDE_ONEWIRE

// 1-Wire ROM family codes for the temperature devices the
// DallasTemperature library knows how to decode. Centralised here so
// the family filter in begin() reads as a list of supported parts.
static inline bool is_temperature_family(uint8_t family) {
  switch (family) {
    case 0x10: // DS18S20 / DS1820
    case 0x22: // DS1822
    case 0x28: // DS18B20
    case 0x3B: // DS1825 / MAX31850
    case 0x42: // DS28EA00
      return true;
    default:
      return false;
  }
}

OneWireSensor::OneWireSensor(uint8_t pin)
  : _bus(pin), _dallas(&_bus) {}

uint8_t OneWireSensor::begin() {
  // Presence-pulse gate. OneWire::reset() returns 1 only when at
  // least one device on the bus pulls the line low in response to
  // the reset pulse. Without it, we skip DallasTemperature entirely.
  if (_bus.reset() != 1) return 0;

  _dallas.begin();

  const uint8_t bus_total = _dallas.getDeviceCount();
  _count = 0;
  for (uint8_t i = 0; i < bus_total && _count < MAX_DEVICES; i++) {
    DeviceAddress addr;
    if (!_dallas.getAddress(addr, i)) continue;

    // Extension point: a future non-temperature family (e.g. DS2438
    // battery monitor, family 0x26) would dispatch on this family
    // byte to its own driver and register a different query_* free
    // function from the manager. The presence-pulse gate above and
    // the per-device address storage below are family-agnostic and
    // can be reused as-is.
    if (!is_temperature_family(addr[0])) continue;

    memcpy(_addresses[_count], addr, sizeof(DeviceAddress));
    _count++;
  }

  return _count;
}

void OneWireSensor::query(uint8_t channel, uint8_t index, CayenneLPP& lpp) {
  if (index >= _count) return;

  // Per-device blocking conversion keeps query() stateless and
  // independent of call order — the cost is one conversion window
  // (~750 ms at default 12-bit resolution) per device per cycle,
  // which matches the synchronous read pattern used by the other
  // sensors in EnvironmentSensorManager.
  _dallas.requestTemperaturesByAddress(_addresses[index]);
  const float t = _dallas.getTempC(_addresses[index]);
  if (t == DEVICE_DISCONNECTED_C) return;

  lpp.addTemperature(channel, t);
}

#endif // ENV_INCLUDE_ONEWIRE
