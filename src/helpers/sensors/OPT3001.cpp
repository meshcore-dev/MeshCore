#include "OPT3001.h"

bool OPT3001::readReg(uint8_t reg, uint16_t& value) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    if (_wire->endTransmission() != 0) return false;

    if (_wire->requestFrom(_addr, (uint8_t)2) != 2) return false;
    uint16_t msb = _wire->read(); // sequence the two reads
    uint16_t lsb = _wire->read();
    value = (msb << 8) | lsb;
    return true;
}

bool OPT3001::writeReg(uint8_t reg, uint16_t value) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write((uint8_t)(value >> 8));
    _wire->write((uint8_t)(value & 0xFF));
    return _wire->endTransmission() == 0;
}

bool OPT3001::probe(TwoWire* wire, uint8_t addr) {
    _wire = wire;
    _addr = addr;

    uint16_t id;
    if (!readReg(OPT3001_REG_MANUFACTURER_ID, id) || id != OPT3001_MANUFACTURER_ID) return false;
    if (!readReg(OPT3001_REG_DEVICE_ID, id) || id != OPT3001_DEVICE_ID) return false;
    return true;
}

bool OPT3001::begin() {
    _ready = false;
    return writeReg(OPT3001_REG_CONFIG, OPT3001_CONFIG_CONTINUOUS);
}

bool OPT3001::readLux(float& lux) {
    if (!_ready) {
        // Reading the latched config register clears CRF, however
        // in continuous mode it is set again after every conversion.
        // We only need to gate the very first read after begin().
        uint16_t config;
        if (!readReg(OPT3001_REG_CONFIG, config)) return false;
        if ((config & OPT3001_CONFIG_CRF) == 0) return false;
        _ready = true;
    }

    uint16_t raw;
    if (!readReg(OPT3001_REG_RESULT, raw)) return false;

    // Result format: exponent E in [15:12], mantissa R in [11:0]
    // lux = 0.01 * 2^E * R (full-scale 83865.6 lux)
    lux = 0.01f * (float)(1u << (raw >> 12)) * (float)(raw & 0x0FFF);
    return true;
}