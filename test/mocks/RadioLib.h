#pragma once

#include <stddef.h>
#include <stdint.h>

#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_UNKNOWN -1
#define LORA_SF 10

inline long random(long min, long max) {
  return min < max ? min : max;
}

class PhysicalLayer {
public:
  virtual ~PhysicalLayer() = default;
  virtual void setPacketReceivedAction(void (*func)(void)) { (void)func; }
  virtual long random(long max) { return max; }
  virtual void setOutputPower(int8_t dbm) { (void)dbm; }
  virtual void standby() { }
  virtual void sleep() { }
  virtual int16_t setPreambleLength(size_t len) {
    (void)len;
    return RADIOLIB_ERR_NONE;
  }
  virtual int16_t startReceive() { return RADIOLIB_ERR_NONE; }
  virtual size_t getPacketLength(bool update = true) {
    (void)update;
    return 0;
  }
  virtual int16_t readData(uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return RADIOLIB_ERR_NONE;
  }
  virtual unsigned long getTimeOnAir(size_t len) {
    (void)len;
    return 0;
  }
  virtual int16_t startTransmit(uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return RADIOLIB_ERR_NONE;
  }
  virtual void finishTransmit() { }
  virtual uint8_t randomByte() { return 0; }
  virtual float getRSSI() { return RADIOLIB_ERR_UNKNOWN; }
  virtual float getSNR() { return RADIOLIB_ERR_UNKNOWN; }
};
