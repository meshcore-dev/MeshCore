#pragma once

#include <cstdint>

namespace mesh {

class Radio {
public:
  virtual ~Radio() = default;
  virtual bool isReceiving() { return false; }
  virtual uint32_t getEstAirtimeFor(uint16_t) { return 10; }
  virtual bool startSendRaw(const uint8_t*, uint16_t) { return true; }
  virtual bool isSendComplete() { return true; }
  virtual void onSendFinished() {}
  virtual int16_t getNoiseFloor() { return -120; }
  virtual void resetAGC() {}
};

class MainBoard {
public:
  virtual ~MainBoard() = default;
  virtual uint16_t getBattMilliVolts() { return 4200; }
  virtual float getMCUTemperature() { return 25.0f; }
  virtual const char* getManufacturerName() { return "mock-board"; }
  virtual void reboot() {}
  virtual bool canControlLoRaFemLna() const { return false; }
  virtual bool setLoRaFemLnaEnabled(bool) { return false; }
  virtual bool isLoRaFemLnaEnabled() const { return false; }
  virtual bool canControlLoRaFemPaGain() const { return false; }
  virtual bool setLoRaFemPaGainEnabled(bool) { return false; }
  virtual bool isLoRaFemPaGainEnabled() const { return false; }
};

}
