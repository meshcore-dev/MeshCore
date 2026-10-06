#include "Oled.h"
#include <Arduino.h>
#include <string.h>
#include <project.h>

static const uint8_t ADDR = 0x3C;

// Direct use of the PSoC I2C master block (what Wire does underneath): Wire.cpp holds 2 x 128 B buffers per instance,
// which this firmware cannot afford in RAM. Timeout 10 ms per byte, same as the core.
static const uint32_t I2C_TIMEOUT_MS = 10;

static bool i2c_write(const uint8_t* b, uint8_t n) {
  I2C_I2CMasterClearStatus();
  if (I2C_I2CMasterSendStart(ADDR, I2C_I2C_WRITE_XFER_MODE, I2C_TIMEOUT_MS) != I2C_I2C_MSTR_NO_ERROR) {
    I2C_I2CMasterSendStop(I2C_TIMEOUT_MS);
    return false;
  }
  bool ok = true;
  for (uint8_t i = 0; i < n && ok; i++) ok = I2C_I2CMasterWriteByte(b[i], I2C_TIMEOUT_MS) == I2C_I2C_MSTR_NO_ERROR;
  I2C_I2CMasterSendStop(I2C_TIMEOUT_MS);
  return ok;
}

bool OledPanel::cmd(uint8_t c) {
  const uint8_t b[2] = { 0x80, c };                              // control byte: single command
  return i2c_write(b, 2);
}

bool OledPanel::cmds(const uint8_t* c, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) if (!cmd(c[i])) return false;
  return true;
}

bool OledPanel::init_panel() {
  static const uint8_t seq[] = {
    0xAE,              // display off
    0xD5, 0xF0,        // clock divide / oscillator
    0xA8, 63,          // multiplex 1/64
    0xD3, 0x00,        // display offset
    0x40,              // start line 0
    0x8D, 0x14,        // charge pump on
    0x20, 0x00,        // horizontal addressing
    0xA1,              // segment re-map
    0xC8,              // COM scan direction
    0xDA, 0x12,        // COM pins (128x64)
    0x81, 0xCF,        // contrast
    0xD9, 0xF1,        // pre-charge
    0xDB, 0x40,        // VCOMH
    0xA4,              // resume RAM content
    0xA6,              // normal (not inverted)
    0x2E               // stop scroll
  };
  return cmds(seq, sizeof(seq));
}

bool OledPanel::begin() {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);                                       // panel power on
  delay(50);
  pinMode(GPIO10, OUTPUT);
  digitalWrite(GPIO10, LOW);                                     // reset pulse
  delay(20);
  digitalWrite(GPIO10, HIGH);
  delay(20);
  if (!_wire) {                                                  // once only
    I2C_SCBCLK_DIV_REG = (uint32_t)(CYDEV_BCLK__HFCLK__HZ / 8 / 400000 / 3) << 8;
    I2C_Start();
    _wire = true;
  }
  if (!i2c_write(0, 0)) {                                        // no acknowledge: panel absent or unpowered
    _present = false;
    digitalWrite(Vext, HIGH);
    return false;
  }
  _present = init_panel();
  _on = false;
  if (!_present) digitalWrite(Vext, HIGH);
  return _present;
}

void OledPanel::power(bool on) {
  if (!_present) return;
  if (on == _on) return;
  if (on) {
    digitalWrite(Vext, LOW);
    delay(20);
    digitalWrite(GPIO10, LOW);
    delay(10);
    digitalWrite(GPIO10, HIGH);
    delay(10);
    if (!init_panel()) { _present = false; digitalWrite(Vext, HIGH); return; }
    cmd(0xAF);                                                   // display on
    _on = true;
  } else {
    cmd(0xAE);                                                   // display off
    cmd(0x8D);
    cmd(0x10);                                                   // charge pump off
    digitalWrite(Vext, HIGH);
    _on = false;
  }
}

bool OledPanel::write_page(uint8_t page, const uint8_t data[128]) {
  if (!_present || !_on || page > 7) return false;
  if (!cmd(0x21) || !cmd(0) || !cmd(127)) return false;          // column window 0..127
  if (!cmd(0x22) || !cmd(page) || !cmd(page)) return false;      // page window
  uint8_t b[17];
  b[0] = 0x40;                                                   // control byte: data
  for (int i = 0; i < 128; i += 16) {
    memcpy(b + 1, data + i, 16);
    if (!i2c_write(b, 17)) return false;
  }
  return true;
}
