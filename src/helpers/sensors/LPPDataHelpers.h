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

class LPPReader {
  const uint8_t* _buf;
  uint8_t _len;
  uint8_t _pos;

  float getFloat(const uint8_t * buffer, uint8_t size, uint32_t multiplier, bool is_signed) {
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
    lat = getFloat(&_buf[_pos], 3, 10000, true); _pos += 3;
    lon = getFloat(&_buf[_pos], 3, 10000, true); _pos += 3;
    alt = getFloat(&_buf[_pos], 3, 100, true); _pos += 3;
    return _pos <= _len;
  }
  bool readVoltage(float& voltage) {
    voltage = getFloat(&_buf[_pos], 2, 100, false); _pos += 2;
    return _pos <= _len;
  }
  bool readCurrent(float& amps) {
    amps = getFloat(&_buf[_pos], 2, 1000, true); _pos += 2;
    return _pos <= _len;
  }
  bool readPower(float& watts) {
    watts = getFloat(&_buf[_pos], 2, 1, false); _pos += 2;
    return _pos <= _len;
  }
  bool readTemperature(float& degrees_c) {
    degrees_c = getFloat(&_buf[_pos], 2, 10, true); _pos += 2;
    return _pos <= _len;
  }
  bool readPressure(float& pa) {
    pa = getFloat(&_buf[_pos], 2, 10, false); _pos += 2;
    return _pos <= _len;
  }
  bool readRelativeHumidity(float& pct) {
    pct = getFloat(&_buf[_pos], 1, 2, false); _pos += 1;
    return _pos <= _len;
  }
  bool readAltitude(float& m) {
    m = getFloat(&_buf[_pos], 2, 1, true); _pos += 2;
    return _pos <= _len;
  }

  void skipData(uint8_t type) {
    switch (type) {
      case LPP_GPS:
        _pos += 9; break;
      case LPP_POLYLINE:
        _pos += 8; break;  // TODO: this is MINIMIUM
      case LPP_GYROMETER:
      case LPP_ACCELEROMETER:
        _pos += 6; break;
      case LPP_GENERIC_SENSOR:
      case LPP_FREQUENCY:
      case LPP_DISTANCE:
      case LPP_ENERGY:
      case LPP_UNIXTIME:
        _pos += 4; break;
      case LPP_COLOUR:
        _pos += 3; break;
      case LPP_ANALOG_INPUT:
      case LPP_ANALOG_OUTPUT:
      case LPP_LUMINOSITY:
      case LPP_TEMPERATURE:
      case LPP_CONCENTRATION:
      case LPP_BAROMETRIC_PRESSURE:
      case LPP_ALTITUDE:
      case LPP_VOLTAGE:
      case LPP_CURRENT:
      case LPP_DIRECTION:
      case LPP_POWER:
        _pos += 2; break;
      default:
        _pos++;
    }
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
