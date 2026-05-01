#pragma once

#include <cstdint>

#include "Dispatcher.h"

// Fake radio for native tests.
// Provides successful no-op send/receive behavior without hardware access.
class FakeRadio final : public mesh::Radio {
public:
  int recvRaw(uint8_t*, int) override {
    return 0;
  }

  uint32_t getEstAirtimeFor(int) override {
    return 1;
  }

  float packetScore(float, int) override {
    return 1.0f;
  }

  bool startSendRaw(const uint8_t*, int) override {
    return true;
  }

  bool isSendComplete() override {
    return true;
  }

  void onSendFinished() override {}

  bool isInRecvMode() const override {
    return true;
  }
};
