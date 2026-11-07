#pragma once
#include <Arduino.h>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_CHANNEL_FREE 0
#define LORA_SF 8
class PhysicalLayer {
public:
  int receiveCalls = 0, transmitCalls = 0, readCalls = 0;
  int sleepCalls = 0, scanCalls = 0, finishCalls = 0;
  void (*irq)() = nullptr;
  void setPacketReceivedAction(void (*fn)()) { irq = fn; }
  int setPreambleLength(int) { return 0; }
  int setOutputPower(int) { return 0; }
  int random(int) { return 1; }
  int randomByte() { return 1; }
  int standby() { return 0; }
  int sleep() { sleepCalls++; return 0; }
  int startReceive() { receiveCalls++; return 0; }
  int getPacketLength() { return 1; }
  int readData(uint8_t*, int) { readCalls++; return 0; }
  uint32_t getTimeOnAir(int) { return 30000; }
  int startTransmit(uint8_t*, int) { transmitCalls++; return 0; }
  int finishTransmit() { finishCalls++; return 0; }
  int scanChannel() { scanCalls++; return 0; }
  float getRSSI() { return -100; }
  float getSNR() { return 1; }
};
