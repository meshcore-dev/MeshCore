#include "M5StackC6LBoard.h"

void M5StackC6LBoard::pi4ioWriteByte(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(PI4IO_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t M5StackC6LBoard::pi4ioReadByte(uint8_t reg) {
  Wire.beginTransmission(PI4IO_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(PI4IO_ADDR, (uint8_t)1);
  return Wire.read();
}

void M5StackC6LBoard::initGpioExpander() {
  pi4ioWriteByte(PI4IO_REG_CHIP_RESET, 0xFF);
  delay(10);

  pi4ioReadByte(PI4IO_REG_CHIP_RESET);
  delay(10);

  pi4ioWriteByte(PI4IO_REG_IO_DIR, 0b11000000);
  delay(10);

  pi4ioWriteByte(PI4IO_REG_OUT_H_IM, 0b00111100);
  delay(10);

  pi4ioWriteByte(PI4IO_REG_PULL_SEL, 0b11000011);
  delay(10);

  pi4ioWriteByte(PI4IO_REG_PULL_EN, 0b11000011);
  delay(10);

  pi4ioWriteByte(PI4IO_REG_IN_DEF_STA, 0b00000011);
  delay(10);

  pi4ioWriteByte(PI4IO_REG_INT_MASK, 0b11111100);
  delay(10);

  pi4ioWriteByte(PI4IO_REG_OUT_SET, 0b10000000);
  delay(10);

  pi4ioReadByte(PI4IO_REG_IRQ_STA);

  uint8_t out_set = pi4ioReadByte(PI4IO_REG_OUT_SET);
  out_set |= (1 << 6);
  pi4ioWriteByte(PI4IO_REG_OUT_SET, out_set);
}

void M5StackC6LBoard::begin() {
  ESP32Board::begin();
  initGpioExpander();
}

const char* M5StackC6LBoard::getManufacturerName() const {
  return "M5Stack C6L";
}
