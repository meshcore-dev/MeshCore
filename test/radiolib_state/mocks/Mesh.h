#pragma once
#include <Arduino.h>
#define BD_STARTUP_RX_PACKET 1
#define MAX_TRANS_UNIT 255
#define MESH_DEBUG_PRINTLN(...)
namespace mesh {
class RNG { public: virtual void random(uint8_t*, size_t) = 0; };
class MainBoard {
public:
  int before = 0, after = 0;
  int getStartupReason() { return 0; }
  void onBeforeTransmit() { before++; }
  void onAfterTransmit() { after++; }
};
class Radio {
public:
  virtual ~Radio() = default;
  virtual void begin() = 0;
  virtual int recvRaw(uint8_t*, int) = 0;
  virtual uint32_t getEstAirtimeFor(int) = 0;
  virtual bool startSendRaw(const uint8_t*, int) = 0;
  virtual bool isSendComplete() = 0;
  virtual void onSendFinished() = 0;
  virtual bool isInRecvMode() const = 0;
  virtual bool isReceiving() = 0;
  virtual int getNoiseFloor() const = 0;
  virtual void triggerNoiseFloorCalibrate(int) = 0;
  virtual void setCADEnabled(bool) = 0;
  virtual void resetAGC() = 0;
  virtual void loop() = 0;
  virtual float getLastRSSI() const = 0;
  virtual float getLastSNR() const = 0;
  virtual float packetScore(float, int) = 0;
};
}
