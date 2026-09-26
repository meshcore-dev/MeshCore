#pragma once

#include <stdint.h>

#define LPP_DIGITAL_INPUT 0         // 1 byte
#define LPP_DIGITAL_OUTPUT 1        // 1 byte
#define LPP_ANALOG_INPUT 2          // 2 bytes, 0.01 signed
#define LPP_ANALOG_OUTPUT 3         // 2 bytes, 0.01 signed
#define LPP_GENERIC_SENSOR 100      // 4 bytes, unsigned
#define LPP_LUMINOSITY 101          // 2 bytes, 1 lux unsigned
#define LPP_PRESENCE 102            // 1 byte, bool
#define LPP_TEMPERATURE 103         // 2 bytes, 0.1°C signed
#define LPP_RELATIVE_HUMIDITY 104   // 1 byte, 0.5% unsigned
#define LPP_ACCELEROMETER 113       // 2 bytes per axis, 0.001G
#define LPP_BAROMETRIC_PRESSURE 115 // 2 bytes 0.1hPa unsigned
#define LPP_VOLTAGE 116             // 2 bytes 0.01V unsigned
#define LPP_CURRENT 117             // 2 bytes 0.001A unsigned
#define LPP_FREQUENCY 118           // 4 bytes 1Hz unsigned
#define LPP_PERCENTAGE 120          // 1 byte 1-100% unsigned
#define LPP_ALTITUDE 121            // 2 byte 1m signed
#define LPP_CONCENTRATION 125       // 2 bytes, 1 ppm unsigned
#define LPP_POWER 128               // 2 byte, 1W, unsigned
#define LPP_DISTANCE 130            // 4 byte, 0.001m, unsigned
#define LPP_ENERGY 131              // 4 byte, 0.001kWh, unsigned
#define LPP_DIRECTION 132           // 2 bytes, 1deg, unsigned
#define LPP_UNIXTIME 133            // 4 bytes, unsigned
#define LPP_GYROMETER 134           // 2 bytes per axis, 0.01 °/s
#define LPP_COLOUR 135              // 1 byte per RGB Color
#define LPP_GPS 136                 // 3 byte lon/lat 0.0001 °, 3 bytes alt 0.01 meter
#define LPP_SWITCH 142              // 1 byte, 0/1
#define LPP_POLYLINE 240            // 1 byte size, 1 byte delta factor, 3 byte lon/lat 0.0001° * factor, n (size-8) bytes deltas

// Multipliers
#define LPP_DIGITAL_INPUT_MULT 1
#define LPP_DIGITAL_OUTPUT_MULT 1
#define LPP_ANALOG_INPUT_MULT 100
#define LPP_ANALOG_OUTPUT_MULT 100
#define LPP_GENERIC_SENSOR_MULT 1
#define LPP_LUMINOSITY_MULT 1
#define LPP_PRESENCE_MULT 1
#define LPP_TEMPERATURE_MULT 10
#define LPP_RELATIVE_HUMIDITY_MULT 2
#define LPP_ACCELEROMETER_MULT 1000
#define LPP_BAROMETRIC_PRESSURE_MULT 10
#define LPP_VOLTAGE_MULT 100
#define LPP_CURRENT_MULT 1000
#define LPP_FREQUENCY_MULT 1
#define LPP_PERCENTAGE_MULT 1
#define LPP_ALTITUDE_MULT 1
#define LPP_POWER_MULT 1
#define LPP_DISTANCE_MULT 1000
#define LPP_ENERGY_MULT 1000
#define LPP_DIRECTION_MULT 1
#define LPP_UNIXTIME_MULT 1
#define LPP_GYROMETER_MULT 100
#define LPP_GPS_LAT_LON_MULT 10000
#define LPP_GPS_ALT_MULT 100
#define LPP_SWITCH_MULT 1
#define LPP_CONCENTRATION_MULT 1
#define LPP_COLOUR_MULT 1

#define LPP_ERROR_OK 0
#define LPP_ERROR_OVERFLOW 1
#define LPP_ERROR_UNKOWN_TYPE 2

class LPPData {
public:
  static uint8_t getDataSize(uint8_t type) {
    switch (type) {
      case LPP_GPS:
        return 9;
      case LPP_POLYLINE:
        return 8;  // TODO: this is MINIMIUM
      case LPP_GYROMETER:
      case LPP_ACCELEROMETER:
        return 6;
      case LPP_GENERIC_SENSOR:
      case LPP_FREQUENCY:
      case LPP_DISTANCE:
      case LPP_ENERGY:
      case LPP_UNIXTIME:
        return 4;
      case LPP_COLOUR:
        return 3;
      case LPP_ANALOG_INPUT:
      case LPP_ANALOG_OUTPUT:
      case LPP_LUMINOSITY:
      case LPP_TEMPERATURE:
      case LPP_CONCENTRATION:
      case LPP_BAROMETRIC_PRESSURE:
      case LPP_RELATIVE_HUMIDITY:
      case LPP_ALTITUDE:
      case LPP_VOLTAGE:
      case LPP_CURRENT:
      case LPP_DIRECTION:
      case LPP_POWER:
        return 2;
    }
    return 1;
  }

  static uint32_t getMultiplier(uint8_t type) {
    switch (type) {
      case LPP_CURRENT:
      case LPP_DISTANCE:
      case LPP_ENERGY:
        return 1000;
      case LPP_VOLTAGE:
      case LPP_ANALOG_INPUT:
      case LPP_ANALOG_OUTPUT:
        return 100;
      case LPP_TEMPERATURE:
      case LPP_BAROMETRIC_PRESSURE:
      case LPP_RELATIVE_HUMIDITY:
        return 10;
    }
    return 1;
  }

  static bool isSigned(uint8_t type) {
    return type == LPP_ALTITUDE || type == LPP_TEMPERATURE || type == LPP_GYROMETER ||
        type == LPP_ANALOG_INPUT || type == LPP_ANALOG_OUTPUT || type == LPP_GPS || type == LPP_ACCELEROMETER;
  }

  static float getFloat(const uint8_t * buffer, uint8_t size, uint32_t multiplier, bool is_signed) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++) {
      value = (value << 8) + buffer[i];
    }

    int sign = 1;
    if (is_signed) {
      uint32_t bit = 1ul << ((size * 8) - 1);
      if ((value & bit) == bit) {
        value = (bit << 1) - value;
        sign = -1;
      }
    }
    return sign * ((float) value / multiplier);
  }

  static uint8_t putFloat(uint8_t * dest, float value, uint8_t size, uint32_t multiplier, bool is_signed) {
    // check sign
    bool sign = value < 0;
    if (sign) value = -value;

    // get value to store
    uint32_t v = value * multiplier;

    // format an uint32_t as if it was an int32_t
    if (is_signed & sign) {
      uint32_t mask = (1 << (size * 8)) - 1;
      v = v & mask;
      if (sign) v = mask - v + 1;
    }

    // add bytes (MSB first)
    for (uint8_t i=1; i<=size; i++) {
      dest[size - i] = (v & 0xFF);
      v >>= 8;
    }
    return size;
  }

};

class LPPReader {
  const uint8_t* _buf;
  uint8_t _len;
  uint8_t _pos;

public:
  LPPReader(const uint8_t buf[], uint8_t len) : _buf(buf), _len(len), _pos(0) { }

  void reset() {
    _pos = 0;
  }

  bool readHeader(uint8_t& channel, uint8_t& type) {
    if (_pos + 2 < _len) {
      channel = _buf[_pos++];
      type = _buf[_pos++];

      return channel != 0;   // channel 0 is End-of-data
    }
    return false;  // end-of-buffer
  }

  bool readGPS(float& lat, float& lon, float& alt) {
    lat = LPPData::getFloat(&_buf[_pos], 3, 10000, true); _pos += 3;
    lon = LPPData::getFloat(&_buf[_pos], 3, 10000, true); _pos += 3;
    alt = LPPData::getFloat(&_buf[_pos], 3, 100, true); _pos += 3;
    return _pos <= _len;
  }
  bool readVoltage(float& voltage) {
    voltage = LPPData::getFloat(&_buf[_pos], 2, 100, false); _pos += 2;
    return _pos <= _len;
  }
  bool readCurrent(float& amps) {
    amps = LPPData::getFloat(&_buf[_pos], 2, 1000, true); _pos += 2;
    return _pos <= _len;
  }
  bool readPower(float& watts) {
    watts = LPPData::getFloat(&_buf[_pos], 2, 1, false); _pos += 2;
    return _pos <= _len;
  }
  bool readTemperature(float& degrees_c) {
    degrees_c = LPPData::getFloat(&_buf[_pos], 2, 10, true); _pos += 2;
    return _pos <= _len;
  }
  bool readPressure(float& pa) {
    pa = LPPData::getFloat(&_buf[_pos], 2, 10, false); _pos += 2;
    return _pos <= _len;
  }
  bool readRelativeHumidity(float& pct) {
    pct = LPPData::getFloat(&_buf[_pos], 1, 2, false); _pos += 1;
    return _pos <= _len;
  }
  bool readAltitude(float& m) {
    m = LPPData::getFloat(&_buf[_pos], 2, 1, true); _pos += 2;
    return _pos <= _len;
  }

  void skipData(uint8_t type) {
    _pos += LPPData::getDataSize(type);
  }
};

class LPPWriter {
  uint8_t* _buf;
  uint8_t _max_len;
  uint8_t _len;

  void write(uint16_t value) {
    _buf[_len++] = (value >> 8) & 0xFF;  // MSB
    _buf[_len++] = value & 0xFF;         // LSB
  }

public:
  LPPWriter(uint8_t buf[], uint8_t max_len): _buf(buf), _max_len(max_len), _len(0) { }

  bool writeData(uint8_t channel, uint8_t type, float v) {
    uint8_t sz = LPPData::getDataSize(type);
    bool s = LPPData::isSigned(type);
    uint32_t mul = LPPData::getMultiplier(type);
    if (_len + 2 + sz <= _max_len) {
      _buf[_len++] = channel;
      _buf[_len++] = type;
      _len += LPPData::putFloat(&_buf[_len], v, sz, mul, s);
      return true;
    }
    return false;
  }

  bool writeVoltage(uint8_t channel, float voltage) {
    if (_len + 4 <= _max_len) {
      _buf[_len++] = channel;
      _buf[_len++] = LPP_VOLTAGE;
      uint16_t value = voltage * 100;
      write(value);
      return true;
    }
    return false;
  }

  bool writeGPS(uint8_t channel, float lat, float lon, float alt) {
    if (_len + 11 <= _max_len) {
      _buf[_len++] = channel;
      _buf[_len++] = LPP_GPS;

      int32_t lati = lat * 10000;  // we lose some precision :-(
      int32_t loni = lon * 10000;
      int32_t alti = alt * 100;

      _buf[_len++] = lati >> 16;
      _buf[_len++] = lati >> 8;
      _buf[_len++] = lati;
      _buf[_len++] = loni >> 16;
      _buf[_len++] = loni >> 8;
      _buf[_len++] = loni;
      _buf[_len++] = alti >> 16;
      _buf[_len++] = alti >> 8;
      _buf[_len++] = alti;
      return true;
    }
    return false;
  }

  uint8_t length() { return _len; }
};
