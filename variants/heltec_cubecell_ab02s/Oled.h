#pragma once
#include <stdint.h>

// Minimal SSD1306 128x64 driver over I2C (0x3C) with no frame buffer: the caller writes one 128-byte page at a time.
// Vext (P3_2, LOW = on) powers the panel; GPIO10 is its reset. If the panel does not acknowledge, begin() returns false.
class OledPanel {
  bool _present;
  bool _on;
  bool _wire;
  bool cmd(uint8_t c);
  bool cmds(const uint8_t* c, uint8_t n);
  bool init_panel();
public:
  OledPanel() : _present(false), _on(false), _wire(false) {}
  bool begin();                       // power the panel, reset it, probe 0x3C, run the init sequence (display off)
  bool present() const { return _present; }
  bool is_on() const { return _on; }
  void power(bool on);                // on: Vext + init + DISPLAYON; off: DISPLAYOFF + charge pump off + Vext off
  bool write_page(uint8_t page, const uint8_t data[128]);   // false on I2C error
};
