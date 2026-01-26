#include "Max17261Gauge.h"

#if ENV_INCLUDE_MAX17261
#include <max17261.h>

// Provide Arduino hooks expected by the max17261 driver
extern "C" max17261_err_t max17261_read_word(struct max17261_conf* conf, uint8_t reg, uint16_t* value) {
  const uint8_t addr = 0x36;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return -1;
  }
  int read = Wire.requestFrom((uint8_t)addr, (uint8_t)2, (bool)true);
  if (read != 2) {
    return -2;
  }
  uint8_t first = Wire.read();
  uint8_t second = Wire.read();
  *value = static_cast<uint16_t>(second) << 8 | first;
  return 0;
}

extern "C" max17261_err_t max17261_write_word(struct max17261_conf* conf, uint8_t reg, uint16_t val) {
  const uint8_t addr = 0x36;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(val & 0xFF));
  Wire.write(static_cast<uint8_t>((val >> 8) & 0xFF));
  return (Wire.endTransmission(true) == 0) ? 0 : 1;
}

extern "C" max17261_err_t max17261_delay_ms(struct max17261_conf* conf, uint32_t period) {
  delay(period);
  return 0;
}

class Max17261GaugeImpl : public BatteryGauge {
public:
  Max17261GaugeImpl() : inited(false) { memset(&conf, 0, sizeof(conf)); }
  bool probe(TwoWire &wire) override {
    const uint8_t addr = 0x36;
    wire.beginTransmission(addr);
    wire.write((uint8_t)0x21); // DevName
    if (wire.endTransmission(false) != 0) return false;
    if (wire.requestFrom(addr, (uint8_t)2, (bool)true) != 2) return false;
    uint8_t lsb = wire.read();
    uint8_t msb = wire.read();
    uint16_t devname = static_cast<uint16_t>(msb) << 8 | lsb;
    return devname == 0x3340; // MAX17261
  }
  bool begin(TwoWire &wire) override {
    if (!inited) {
      conf.DesignCap = 5000; // mAh
      conf.IchgTerm = 25;    // mA
      conf.VEmpty = ((3300 / 10) << 7) | ((3880 / 40) & 0x7F);
      conf.R100 = 1;
      conf.ChargeVoltage = 4200; // mV
      if (max17261_init(&conf) == 0) inited = true;
    }
    return inited;
  }
  uint16_t readMillivolts() override {
    if (!inited) return 0;
    return max17261_get_voltage(&conf);
  }
  float readBatteryTemperatureC() override {
    if (!inited) return NAN;
    return max17261_get_die_temperature(&conf);
  }
private:
  bool inited;
  struct max17261_conf conf;
};

BatteryGauge* createMax17261GaugeIfPresent(TwoWire &wire) {
  Max17261GaugeImpl *g = new Max17261GaugeImpl();
  if (g->probe(wire) && g->begin(wire)) {
    return g;
  }
  delete g;
  return nullptr;
}

#endif


