// Build only for Meshimi board-enabled targets
#ifdef USE_MESHIMI_BOARD
#include "MeshimiBoard.h"

// MAX17261 driver
#include <max17261.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

static struct max17261_conf g_max17261_conf;
static bool g_max17261_inited = false;
static bool g_max17261_conf_inited = false;
static uint32_t g_last_mv_read_ms = 0;
static uint16_t g_last_mv_cached = 0;
static uint32_t g_last_temp_read_ms = 0;
static float g_last_temp_cached = NAN;

static constexpr uint8_t kMax17261Address = 0x36;  // I2C address
static uint32_t g_last_init_attempt_ms = 0;
static uint8_t g_init_attempts = 0;
static constexpr uint8_t kMaxInitAttempts = 3;

static inline void ensure_gauge_initialized() {
  if (g_max17261_inited) {
    return;
  }
  if (g_init_attempts >= kMaxInitAttempts) {
    return;
  }
  uint32_t now = millis();
  if ((now - g_last_init_attempt_ms) < 5000) {
    return;
  }
  if (!g_max17261_conf_inited) {
    memset(&g_max17261_conf, 0, sizeof(g_max17261_conf));
    g_max17261_conf.DesignCap = 5000; // mAh
    g_max17261_conf.IchgTerm = 25;    // mA
    g_max17261_conf.VEmpty = ((3300 / 10) << 7) | ((3880 / 40) & 0x7F);
    g_max17261_conf.R100 = 1;
    g_max17261_conf.ChargeVoltage = 4200; // mV
    g_max17261_conf_inited = true;
  }
  Wire.setClock(400000);
  g_last_init_attempt_ms = now;
  g_init_attempts++;
  if (max17261_init(&g_max17261_conf) == 0) {
    g_max17261_inited = true;
  }
}

void MeshimiBoard::begin() {
  XiaoC6Board::begin();

  // Basic configuration values; tune for your battery pack if needed
  memset(&g_max17261_conf, 0, sizeof(g_max17261_conf));

  g_max17261_conf.DesignCap = 5000; // mAh
  g_max17261_conf.IchgTerm = 25;    // mA
  g_max17261_conf.VEmpty = ( (3300 / 10) << 7) | ((3880 / 40) & 0x7F);
  g_max17261_conf.R100 = 1;
  g_max17261_conf.ChargeVoltage = 4200; // mV
  g_max17261_conf_inited = true;

  // Ensure I2C bus is running at a reasonable fast mode for fuel gauge
  Wire.setClock(400000);

  g_max17261_inited = (max17261_init(&g_max17261_conf) == 0);
}

uint16_t MeshimiBoard::getBattMilliVolts() {
  ensure_gauge_initialized();

  // Low-rate cache to avoid frequent I2C transactions
  uint32_t now = millis();
  if (g_last_mv_cached != 0 && (now - g_last_mv_read_ms) < 1000) {
    return g_last_mv_cached;
  }

  if (!g_max17261_inited) {
    // fall back to board ADC if available without hitting I2C
    uint16_t fallback = ESP32Board::getBattMilliVolts();
    g_last_mv_cached = fallback;
    g_last_mv_read_ms = now;
    return fallback;
  }

  uint16_t mv = max17261_get_voltage(&g_max17261_conf);
  if (mv == 0) {
    // fall back to board ADC if available
    uint16_t fallback = ESP32Board::getBattMilliVolts();
    g_last_mv_cached = fallback;
    g_last_mv_read_ms = now;
    return fallback;
  }

  g_last_mv_cached = mv;
  g_last_mv_read_ms = now;
  return mv;
}

float MeshimiBoard::getBattTemperatureC() {
  ensure_gauge_initialized();

  // Low-rate cache to avoid frequent I2C transactions
  uint32_t now = millis();
  if (!isnan(g_last_temp_cached) && (now - g_last_temp_read_ms) < 1000) {
    return g_last_temp_cached;
  }

  if (!g_max17261_inited) {
    // Temperature unavailable without gauge
    g_last_temp_cached = NAN;
    g_last_temp_read_ms = now;
    return g_last_temp_cached;
  }

  float t = max17261_get_die_temperature(&g_max17261_conf);
  g_last_temp_cached = t;
  g_last_temp_read_ms = now;
  return t;
}

// Default weak hooks mapping to Arduino Wire/delay
extern "C" max17261_err_t max17261_read_word(struct max17261_conf* conf, uint8_t reg, uint16_t* value) {
  Wire.beginTransmission(kMax17261Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return -1;
  }
  int read = Wire.requestFrom((uint8_t)kMax17261Address, (uint8_t)2, (bool)true);
  if (read != 2) {
    return -2;
  }
  uint8_t first = Wire.read();
  uint8_t second = Wire.read();
  // MAX1726x registers are little-endian (LSB then MSB)
  *value = static_cast<uint16_t>(second) << 8 | first;
  return 0;
}

extern "C" max17261_err_t max17261_write_word(struct max17261_conf* conf, uint8_t reg, uint16_t val) {
  Wire.beginTransmission(kMax17261Address);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(val & 0xFF));
  Wire.write(static_cast<uint8_t>((val >> 8) & 0xFF));
  return (Wire.endTransmission(true) == 0) ? 0 : 1;
}

extern "C" max17261_err_t max17261_delay_ms(struct max17261_conf* conf, uint32_t period) {
  delay(period);
  return 0;
}

#endif // USE_MESHIMI_BOARD
