#include "ThinkNodeM7Board.h"

void ThinkNodeM7Board::begin() {
  ESP32Board::begin();
}

void ThinkNodeM7Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
  esp_deep_sleep_start();
}

void ThinkNodeM7Board::powerOff()  {
  enterDeepSleep(0);
}

const char* ThinkNodeM7Board::getManufacturerName() const {
  return "Elecrow ThinkNode M7";
}
