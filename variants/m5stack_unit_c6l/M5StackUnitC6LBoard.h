#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>

// PI4IO I/O Expander (I2C address 0x43)
// Pin mapping:
//   P0 = Button (active low)
//   P1 = (unused input)
//   P5 = LNA_EN (LNA Enable)
//   P6 = ANT_SW (RF Switch)
//   P7 = NRST (LoRa Reset)
#define PI4IO_ADDR 0x43

// PI4IO registers
#define PI4IO_REG_CHIP_RESET 0x01
#define PI4IO_REG_IO_DIR 0x03
#define PI4IO_REG_OUT_SET 0x05
#define PI4IO_REG_OUT_H_IM 0x07
#define PI4IO_REG_IN_DEF_STA 0x09
#define PI4IO_REG_PULL_EN 0x0B
#define PI4IO_REG_PULL_SEL 0x0D
#define PI4IO_REG_IN_STA 0x0F
#define PI4IO_REG_INT_MASK 0x11
#define PI4IO_REG_IRQ_STA 0x13

class M5StackUnitC6LBoard : public ESP32Board {
private:
  bool i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }

  bool i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *value) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
    if (!Wire.available()) return false;
    *value = Wire.read();
    return true;
  }

  void initIOExpander() {
    // Initialize PI4IO I/O expander for LoRa control pins
    // P5: LNA Enable, P6: RF Switch, P7: LoRa Reset
    uint8_t in_data;

    // Reset the I/O expander
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_CHIP_RESET, 0xFF);
    delay(10);

    i2c_read_byte(PI4IO_ADDR, PI4IO_REG_CHIP_RESET, &in_data);
    delay(10);

    // Set P5, P6, P7 as outputs (0: input, 1: output)
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_IO_DIR, 0b11100000);
    delay(10);

    // Disable High-Impedance for used pins
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_OUT_H_IM, 0b00011100);
    delay(10);

    // Pull up/down select (0: down, 1: up)
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_PULL_SEL, 0b11100011);
    delay(10);

    // Pull up/down enable (0: disable, 1: enable)
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_PULL_EN, 0b11100011);
    delay(10);

    // Default input state for P0, P1 (buttons)
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_IN_DEF_STA, 0b00000011);
    delay(10);

    // Enable interrupts for P0, P1 (0: enable, 1: disable)
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_INT_MASK, 0b11111100);
    delay(10);

    // Set P7 (Reset) high, P5 and P6 will be set after
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_OUT_SET, 0b10000000);
    delay(10);

    // Clear IRQ status
    i2c_read_byte(PI4IO_ADDR, PI4IO_REG_IRQ_STA, &in_data);

    // Enable RF switch (P6 high) and LNA (P5 high)
    i2c_read_byte(PI4IO_ADDR, PI4IO_REG_OUT_SET, &in_data);
    in_data |= (1 << 6);  // P6 = RF Switch = HIGH
    in_data |= (1 << 5);  // P5 = LNA Enable = HIGH
    i2c_write_byte(PI4IO_ADDR, PI4IO_REG_OUT_SET, in_data);
  }

public:
  void begin() {
    ESP32Board::begin();

    // Initialize I/O expander for LoRa RF control
    initIOExpander();

#ifdef PIN_BUZZER
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
#endif
  }

  const char* getManufacturerName() const override {
    return "M5Stack Unit C6L";
  }

  // Read button state from I/O expander P0 (active low)
  bool isButtonPressed() {
    uint8_t in_data = 0xFF;  // Default high (button not pressed)
    if (!i2c_read_byte(PI4IO_ADDR, PI4IO_REG_IN_STA, &in_data)) {
      return false;  // I2C error, assume not pressed
    }
    return !(in_data & 0x01);  // P0 is active low
  }

#ifdef PIN_BUZZER
  void playTone(uint16_t frequency, uint16_t duration_ms) {
    tone(PIN_BUZZER, frequency, duration_ms);
  }

  void stopTone() {
    noTone(PIN_BUZZER);
  }
#endif
};
