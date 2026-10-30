#include "DFRobotLoRaBoard.h"

void DFRobotLoRaBoard::begin() {
  // SX1262 module power. Held high for the whole boot; Companion does not
  // drop into deep sleep, so the pin does not need an RTC hold.
  pinMode(LORA_POWER_EN, OUTPUT);
  digitalWrite(LORA_POWER_EN, HIGH);
  delay(10);

  periph_power.begin();

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  ESP32Board::begin();
}

const char* DFRobotLoRaBoard::getManufacturerName() const {
  return "DFRobot LoRaWAN ESP32-S3";
}
