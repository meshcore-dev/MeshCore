#include "ThinkNodeM9Board.h"

void ThinkNodeM9Board::begin() {

  // power on screen
  pinMode(VEXT_ENABLE, OUTPUT);
  digitalWrite(VEXT_ENABLE, VEXT_ON_VALUE);

  ESP32Board::begin();

}

void ThinkNodeM9Board::powerOff()  {
  enterDeepSleep(0);
}

uint32_t ThinkNodeM9Board::getIRQGpio() {
  return LORA_DIO0;
}

const char* ThinkNodeM9Board::getManufacturerName() const {
  return "Elecrow ThinkNode M9";
}
