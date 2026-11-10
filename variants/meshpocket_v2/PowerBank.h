#pragma once

#include <CW221X.h>
#include <HLT331Serial.h>

struct PowerBankSnapshot {
  CW221X::Status cw{};
  HLT331Serial::Status hlt{};

  bool hltLive() const;
  bool hasChargingInput() const;
};

class PowerBank {
public:
  void begin();
  void poll();
  void end();
  const PowerBankSnapshot& snapshot() const { return status; }
  bool takePageEvent();

private:
  void readGauge();
  void readHlt();
  void updateFault(bool& sourceFault, bool active);

  CW221X gauge;
  HLT331Serial hlt;
  PowerBankSnapshot status;
  HLT331Serial::Status previous{};
  bool gaugeStarted = false;
  bool hltStarted = false;
  bool cwFault = false;
  bool hltFault = false;
  bool pageEvent = false;
  bool draining = false;
  uint8_t fastGaugeReads = 0;
  uint32_t seenPulses = 0;
  uint32_t drainStarted = 0;
  uint32_t lastPoll = 0;
  uint32_t lastHlt = 0;
  uint32_t lastGauge = 0;
};
