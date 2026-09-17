#pragma once

#include <helpers/radiolib/RadioLibWrappers.h>

// Stub the actual chip-specific layer.
class CustomSX1262 : public PhysicalLayer {
public:
  int16_t scan_start_error = RADIOLIB_ERR_NONE;
  int16_t scan_result = RADIOLIB_CHANNEL_FREE;
  unsigned scan_results = 0;
  int16_t startChannelScan() { return scan_start_error; }
  int16_t getChannelScanResult() { scan_results++; return scan_result; }
};

class CustomSX1262Wrapper : public RadioLibWrapper {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board)
    : RadioLibWrapper(radio, board) { }
  void setParams(float, float, uint8_t, uint8_t) override { }
  bool isReceivingPacket() override { return false; }
  float getCurrentRSSI() override { return -100; }
};
