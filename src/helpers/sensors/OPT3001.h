/**
 * @file OPT3001.h
 * @author Nick Dunklee
 * @brief Minimal driver for the TI OPT3001 ambient light sensor
 *        (used on the RAK1903 WisBlock module).
 *
 * Written for MeshCore from the TI OPT3001 datasheet (SBOS681).
 * https://www.ti.com/lit/ds/sbos681b/sbos681b.pdf
 *
 * The default address of 0x44 is shared with other sensors (SHT4x,
 * INA226), so probe() performs a read-only identity check and no
 * register is written until the chip is positively ID'd.
*/

#pragma once

#include <Arduino.h>
#include <Wire.h>

#define OPT3001_REG_RESULT          0x00
#define OPT3001_REG_CONFIG          0x01
#define OPT3001_REG_MANUFACTURER_ID 0x7E
#define OPT3001_REG_DEVICE_ID       0x7F

#define OPT3001_MANUFACTURER_ID     0x5449  // TI
#define OPT3001_DEVICE_ID           0x3001

// Automatic full-scale range, 800ms conversions, continuous mode, latched flags
#define OPT3001_CONFIG_CONTINUOUS   0xCE10
#define OPT3001_CONFIG_CRF          0x0080  // conversion ready flag

class OPT3001 {
    TwoWire* _wire;
    uint8_t  _addr;
    bool     _ready; // at least one conversion has completed since begin()

    bool readReg(uint8_t reg, uint16_t& value);
    bool writeReg(uint8_t reg, uint16_t value);

public:
    OPT3001() : _wire(NULL), _addr(0), _ready(false) { }

    // Read-only identity check (manufacturer + device ID). Safe to call
    // on a foreign device that happens to share the I2C address.
    bool probe(TwoWire* wire, uint8_t addr);

    // Start continuous conversions. Call only after probe() succeeds.
    bool begin();

    // Return false until the first conversion completes.
    bool readLux(float& lux);
};