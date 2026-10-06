#pragma once
#include <Arduino.h>
#include <MeshCore.h>
#include "EpochClock.h"

class CubeCellRTC : public mesh::RTCClock {
  asr650x::EpochClock _clock;
public:
  uint32_t getCurrentTime() override { return _clock.now(millis()); }
  void setCurrentTime(uint32_t t) override { _clock.set(t, millis()); }
  bool synced() const { return _clock.synced(); }
};
