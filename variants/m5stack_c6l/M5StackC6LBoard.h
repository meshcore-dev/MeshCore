#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>

#define PI4IO_ADDR 0x43

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

class M5StackC6LBoard : public ESP32Board {
public:
  void begin();
  const char* getManufacturerName() const override;

#ifdef P_LORA_TX_NEOPIXEL
  void onBeforeTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL, 64, 64, 64);
  }
  void onAfterTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL, 0, 0, 0);
  }
#endif

private:
  void pi4ioWriteByte(uint8_t reg, uint8_t value);
  uint8_t pi4ioReadByte(uint8_t reg);
  void initGpioExpander();
};
