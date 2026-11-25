#include <Arduino.h>
#include "ThinkNodeM4Board.h"
#include <Wire.h>

#include <bluefruit.h>

void ThinkNodeM4Board::begin() {
  NRF52Board::begin();
  btn_prev_state = HIGH;

  Wire.begin();
  battery_serial->begin(4800);

  delay(10);   // give sx1262 some time to power up
}

uint16_t ThinkNodeM4Board::getBattMilliVolts() {
  int tentatives = 10;
  uint8_t data[5];
  uint8_t b;

  if (battery_serial->available() < 10)
    return bat_level_mv;

  // discard old data
  while (battery_serial->available() > 10)
    battery_serial->read();

  // synchronize
  while((b = battery_serial->read()) != 0xFE) {
    if (tentatives-- == 0) {
      MESH_DEBUG_PRINTLN("Could not find battery frame start %x", b);
      return bat_level_mv;
    }
  }
  battery_serial->readBytes(data, 5);

  if (data[4] != 0xFD) {
    MESH_DEBUG_PRINTLN("Invalid battery frame end %x", data[4]);
    return bat_level_mv;
  }
  bat_level_percent = data[0];
  //MESH_DEBUG_PRINTLN("Battery level %d\%", bat_level_percent);
  bat_level_mv = 2 * (data[1]*1000. + data[2]*10. + data[3]/10.);
  return bat_level_mv;
}
