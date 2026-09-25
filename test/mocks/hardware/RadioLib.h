#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstring>
#include <vector>

#define RADIOLIB_NC UINT32_MAX
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_CHANNEL_FREE -15
#define RADIOLIB_LORA_DETECTED -702
#define RADIOLIB_ERR_RX_TIMEOUT -6
#define RADIOLIB_ERR_SPI_CMD_FAILED -707

class SPIClass { };
class SPISettings {
public:
  SPISettings(uint32_t, uint8_t, uint8_t) { }
};

class ArduinoHal {
public:
  unsigned native_calls = 0;
  uint32_t last_pin = RADIOLIB_NC;
  ArduinoHal(SPIClass&, SPISettings) { }
  virtual void pinMode(uint32_t pin, uint32_t) { native_calls++; last_pin = pin; }
  virtual void digitalWrite(uint32_t pin, uint32_t) { native_calls++; last_pin = pin; }
  virtual uint32_t digitalRead(uint32_t pin) { native_calls++; last_pin = pin; return LOW; }
  virtual uint32_t pinToInterrupt(uint32_t pin) { return pin; }
  virtual void attachInterrupt(uint32_t pin, void (*)(), uint32_t) { native_calls++; last_pin = pin; }
  virtual void detachInterrupt(uint32_t pin) { native_calls++; last_pin = pin; }
};

class PhysicalLayer {
public:
  void (*callback)() = nullptr;
  bool pending = false;
  int rx_starts = 0, reads = 0, sleeps = 0;
  int rx_error = 0, tx_error = 0, read_error = 0;
  std::vector<uint8_t> packet{0x12, 0x34, 0x56};

  void setPacketReceivedAction(void (*action)()) { callback = action; }
  void setPreambleLength(uint16_t) { }
  int16_t sleep() { sleeps++; return 0; }
  int16_t standby() { return 0; }
  int16_t startReceive() { rx_starts++; pending = false; return rx_error; }
  size_t getPacketLength() { return packet.size(); }
  int16_t readData(uint8_t* bytes, size_t len) {
    reads++;
    pending = false;
    memcpy(bytes, packet.data(), len);
    return read_error;
  }
  uint32_t getTimeOnAir(int) { return 100000; }
  int16_t startTransmit(uint8_t*, int) { pending = false; return tx_error; }
  int16_t finishTransmit() { pending = false; return 0; }
  int16_t scanChannel() { if (callback) callback(); pending = true; return RADIOLIB_CHANNEL_FREE; }
  float getRSSI() { return -100; }
  float getSNR() { return 8; }
  int16_t setOutputPower(int8_t) { return 0; }
  long random(long) { return 42; }
  uint8_t randomByte() { return 42; }
};
