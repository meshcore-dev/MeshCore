/*
  This file is part of the Arduino_LPS22HB library.
  Copyright (c) 2019 Arduino SA. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <Wire.h>

#include "BARO.h"

#define LPS22HB_ADDRESS  0x5C

#define LPS22HB_WHO_AM_I_REG        0x0f
#define LPS22HB_CTRL2_REG           0x11
#define LPS22HB_STATUS_REG          0x27
#define LPS22HB_PRESS_OUT_XL_REG    0x28
#define LPS22HB_PRESS_OUT_L_REG     0x29
#define LPS22HB_PRESS_OUT_H_REG     0x2a
#define LPS22HB_TEMP_OUT_L_REG      0x2b
#define LPS22HB_TEMP_OUT_H_REG      0x2c

LPS22HBClass::LPS22HBClass(TwoWire& wire) :
  _wire(&wire),
  _initialized(false)
{
}

int LPS22HBClass::begin()
{
  if (i2cRead(LPS22HB_WHO_AM_I_REG) != 0xb1) {
    _initialized = false;
    return 0;
  }

  _initialized = true;
  return 1;
}

void LPS22HBClass::end()
{
  _initialized = false;
}

float LPS22HBClass::readPressure(int units)
{
  if (_initialized == true) {
    // trigger one shot
    i2cWrite(LPS22HB_CTRL2_REG, 0x01);

    // wait for ONE_SHOT bit to be cleared by the hardware
    while ((i2cRead(LPS22HB_CTRL2_REG) & 0x01) != 0) {
      yield();
    }

    float reading = (i2cRead(LPS22HB_PRESS_OUT_XL_REG) |
            (i2cRead(LPS22HB_PRESS_OUT_L_REG) << 8) |
            (i2cRead(LPS22HB_PRESS_OUT_H_REG) << 16)) / 40960.0;

    if (units == MILLIBAR) { // 1 kPa = 10 millibar
      return reading * 10;
    } else if (units == PSI) {  // 1 kPa = 0.145038 PSI
      return reading * 0.145038;
    } else {
      return reading;
    }
  }
  return 0;
}

float LPS22HBClass::readTemperature(void)
{
  float reading = (i2cRead(LPS22HB_TEMP_OUT_L_REG) << 0) | 
          (i2cRead(LPS22HB_TEMP_OUT_H_REG) << 8);

  return reading/100;
}

int LPS22HBClass::i2cRead(uint8_t reg)
{
  _wire->beginTransmission(LPS22HB_ADDRESS);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) {
    return -1;
  }

  if (_wire->requestFrom(LPS22HB_ADDRESS, 1) != 1) {
    return -1;
  }
  
  return _wire->read();
}

int LPS22HBClass::i2cWrite(uint8_t reg, uint8_t val)
{
  _wire->beginTransmission(LPS22HB_ADDRESS);
  _wire->write(reg);
  _wire->write(val);
  if (_wire->endTransmission() != 0) {
    return 0;
  }

  return 1;
}

#ifdef ARDUINO_ARDUINO_NANO33BLE
LPS22HBClass BARO(Wire1);
#else
LPS22HBClass BARO(Wire);
#endif
