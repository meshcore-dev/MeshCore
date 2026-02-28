
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>

// PI4IOE5V6408 I2C GPIO expander - controls RF switch, LNA, and LoRa reset
// Sits on the internal I2C bus (SDA=10, SCL=8), which is configured as
// the primary Wire bus via PIN_BOARD_SDA/SCL in platformio.ini.
// ESP32-C6 only has ONE I2C hardware peripheral - do NOT use TwoWire(1).

#define PI4IO_ADDR            0x43
#define PI4IO_REG_CHIP_RESET  0x01
#define PI4IO_REG_IO_DIR      0x03
#define PI4IO_REG_OUT_SET     0x05
#define PI4IO_REG_OUT_H_IM    0x07
#define PI4IO_REG_IN_DEF_STA  0x09
#define PI4IO_REG_PULL_EN     0x0B
#define PI4IO_REG_PULL_SEL    0x0D
#define PI4IO_REG_INT_MASK    0x11
#define PI4IO_REG_IRQ_STA     0x13

class UnitC6LBoard : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();  // This calls Wire.begin(10, 8) via PIN_BOARD_SDA/SCL
    initGPIOExpander();
  }

  const char* getManufacturerName() const override {
    return "Unit C6L";
  }

private:
  void i2cWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(PI4IO_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

  uint8_t i2cRead(uint8_t reg) {
    Wire.beginTransmission(PI4IO_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)PI4IO_ADDR, (uint8_t)1);
    return Wire.read();
  }

  bool i2cProbe() {
    Wire.beginTransmission(PI4IO_ADDR);
    return Wire.endTransmission() == 0;
  }

  void initGPIOExpander() {
    // Matches Meshtastic's c6l_init() in variant.cpp
    // Uses Wire (already on SDA=10, SCL=8 from ESP32Board::begin)

    if (!i2cProbe()) {
      return;
    }

    // Reset expander
    i2cWrite(PI4IO_REG_CHIP_RESET, 0xFF);
    delay(10);
    i2cRead(PI4IO_REG_CHIP_RESET);
    delay(10);

    // P6 (RF switch) and P7 (LoRa reset) as outputs
    i2cWrite(PI4IO_REG_IO_DIR, 0b11000000);
    delay(10);

    // Disable high-impedance on P2-P5
    i2cWrite(PI4IO_REG_OUT_H_IM, 0b00111100);
    delay(10);

    // Pull-up on P0, P1, P6, P7; pull-down on others
    i2cWrite(PI4IO_REG_PULL_SEL, 0b11000011);
    delay(10);
    i2cWrite(PI4IO_REG_PULL_EN, 0b11000011);
    delay(10);

    // Button defaults (P0, P1 default HIGH - active low buttons)
    i2cWrite(PI4IO_REG_IN_DEF_STA, 0b00000011);
    delay(10);

    // Interrupt mask: only P0, P1 generate interrupts
    i2cWrite(PI4IO_REG_INT_MASK, 0b11111100);
    delay(10);

    // Set P7 HIGH (LoRa out of reset)
    i2cWrite(PI4IO_REG_OUT_SET, 0b10000000);
    delay(10);

    // Clear pending IRQ
    i2cRead(PI4IO_REG_IRQ_STA);

    // Set P6 HIGH (RF switch -> routes antenna to LoRa)
    uint8_t out = i2cRead(PI4IO_REG_OUT_SET);
    out |= (1 << 6);
    i2cWrite(PI4IO_REG_OUT_SET, out);

  }
};
