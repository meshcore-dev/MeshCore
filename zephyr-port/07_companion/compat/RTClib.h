/*
 * Minimal RTClib shim for the Zephyr companion. MeshCore only uses DateTime (epoch
 * <-> Y/M/D h:m:s); the real RTClib pulls Adafruit_I2CDevice/Wire. Header-only.
 */
#pragma once
#include <stdint.h>
#include <time.h>

class DateTime {
	uint32_t _epoch;
	struct tm _tm;
  public:
	DateTime(uint32_t t = 0) : _epoch(t) {
		time_t tt = (time_t)t;
		struct tm *r = gmtime(&tt);
		if (r) _tm = *r; else { _tm = {}; }
	}
	uint32_t unixtime() const { return _epoch; }
	uint16_t year() const   { return (uint16_t)(_tm.tm_year + 1900); }
	uint8_t  month() const  { return (uint8_t)(_tm.tm_mon + 1); }
	uint8_t  day() const    { return (uint8_t)_tm.tm_mday; }
	uint8_t  hour() const   { return (uint8_t)_tm.tm_hour; }
	uint8_t  minute() const { return (uint8_t)_tm.tm_min; }
	uint8_t  second() const { return (uint8_t)_tm.tm_sec; }
};
