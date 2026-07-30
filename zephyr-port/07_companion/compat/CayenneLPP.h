/*
 * Minimal CayenneLPP shim for the Zephyr companion. The companion/SensorManager only
 * use reset/addVoltage/getBuffer/getSize; the real lib pulls STL <map> (libstdc++).
 * This header-only shim keeps the build on picolibc + the Arduino min/max macros.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifndef MC_LPP_BUF
#define MC_LPP_BUF 64
#endif

class CayenneLPP {
	uint8_t _buf[MC_LPP_BUF];
	uint8_t _pos;
  public:
	explicit CayenneLPP(uint8_t size = MC_LPP_BUF) { (void)size; _pos = 0; }
	void reset() { _pos = 0; }
	uint8_t *getBuffer() { return _buf; }
	uint8_t getSize() { return _pos; }
	uint8_t getError() { return 0; }

	/* Cayenne LPP data channels the companion may emit (channel,type,payload). */
	uint8_t addVoltage(uint8_t ch, float v)            { return put(ch, 0x74, (int32_t)(v * 100), 2); }
	uint8_t addTemperature(uint8_t ch, float t)        { return put(ch, 0x67, (int32_t)(t * 10), 2); }
	uint8_t addRelativeHumidity(uint8_t ch, float h)   { return put(ch, 0x68, (int32_t)(h * 2), 1); }
	uint8_t addAnalogInput(uint8_t ch, float a)        { return put(ch, 0x02, (int32_t)(a * 100), 2); }
	uint8_t addDigitalInput(uint8_t ch, uint32_t d)    { return put(ch, 0x00, (int32_t)d, 1); }

  private:
	uint8_t put(uint8_t ch, uint8_t type, int32_t val, uint8_t nbytes) {
		if (_pos + 2 + nbytes > (int)sizeof(_buf)) return 0;
		_buf[_pos++] = ch;
		_buf[_pos++] = type;
		for (int i = nbytes - 1; i >= 0; --i) _buf[_pos++] = (uint8_t)(val >> (8 * i));
		return 1;
	}
};
