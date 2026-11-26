#pragma once
#include <RadioProfiles.h>
namespace mesh {
class Packet { public: uint32_t transmit_token = 0; };
class RTCClock { public: uint32_t epoch = 1700000000; uint32_t getCurrentTime() { return epoch; } };
class Radio {
public:
  RadioProfiles state;
  RadioParamApplyResult result = RadioParamApplyResult::APPLIED;
  bool supported = true;
  unsigned applies = 0;
  RadioProfiles* profiles() { return supported ? &state : nullptr; }
  bool validateProfile(const RadioProfileParams& p) const { return RadioProfiles::valid(p); }
  RadioParamApplyResult trySetPrimaryParams(const RadioProfileParams& p, bool temp = false) {
    ++applies;
    if (result == RadioParamApplyResult::APPLIED) state.setPrimary(p, temp);
    return result;
  }
  uint16_t profilePreamble(uint8_t p) const { return state.params(p).preamble; }
};
class Dispatcher {
  uint32_t next = 0;
public:
  uint32_t reserveTransmitToken() { return ++next; }
  void setTransmitToken(Packet* packet, uint32_t token) { packet->transmit_token = token; }
};
}
