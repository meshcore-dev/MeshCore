#include "TLoRaPagerBoard.h"
#include <SPI.h>

namespace {
constexpr uint8_t Expander = 0x20;
constexpr uint8_t BatteryGauge = 0x55;

bool readRegister(uint8_t address, uint8_t reg, uint8_t* result, uint8_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, length) != length)
    return false;
  for (uint8_t i = 0; i < length; ++i) result[i] = Wire.read();
  return true;
}

bool updateExpander(uint8_t reg, uint8_t mask, uint8_t values) {
  uint8_t current;
  if (!readRegister(Expander, reg, &current, 1)) return false;
  Wire.beginTransmission(Expander);
  Wire.write(reg);
  Wire.write((current & ~mask) | (values & mask));
  return Wire.endTransmission() == 0;
}
}

void TLoRaPagerBoard::begin() {
  ESP32Board::begin();
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  // Park LCD, LoRa, SD and NFC before attaching the shared SPI peripheral.
  const uint8_t selects[] = {PIN_TFT_CS, P_LORA_NSS, 21, 39};
  for (uint8_t pin : selects) {
    digitalWrite(pin, HIGH);
    pinMode(pin, OUTPUT);
  }
  digitalWrite(P_LORA_RESET, HIGH);
  pinMode(P_LORA_RESET, OUTPUT);

  // XL9555 outputs 0..7 power peripherals and control their resets. On port 1
  // only keyboard, external GPIO and SD power are outputs (bits 0, 1, 4).
  // Load the output latches before changing direction to prevent glitches.
  constexpr uint8_t panelReset = 1U << 6;
  if (!updateExpander(2, 0xff, 0xff & ~panelReset) ||
      !updateExpander(3, 0x13, 0x13) ||
      !updateExpander(6, 0xff, 0) || !updateExpander(7, 0x13, 0)) return;
  delay(50);
  if (!updateExpander(2, panelReset, panelReset)) return;
  delay(50);
  SPI.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  _ready = SPI.bus() != nullptr;
}

uint16_t TLoRaPagerBoard::getBattMilliVolts() {
  // BQ27220 standard Voltage() command returns millivolts, least significant byte first.
  uint8_t data[2];
  return readRegister(BatteryGauge, 0x08, data, 2) ? uint16_t(data[1] << 8 | data[0]) : 0;
}
