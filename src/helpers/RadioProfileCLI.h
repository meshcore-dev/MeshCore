#pragma once

#include <RadioProfiles.h>
#include "ConfigSerializer.h"

class CommonRadioPrefs;

namespace mesh {
class Radio;
class RTCClock;
class Dispatcher;
class Packet;

// One scheduler/controller shared by the four mesh roles. Only the saved
// secondary, crossover setting and primary preamble enter the existing prefs.
// Leases, pending applications and the four one-slot schedules stay in RAM.
class RadioProfileCLI : public ConfigSerializer {
  struct Pending {
    RadioProfileConfig config;
    uint32_t due = 0, end = 0, retry = 0;
    int8_t schedule = -1;
    bool active = false, temporary = false, save = false;
  } pending_[2];
  struct Lease { uint32_t end = 0; int8_t schedule = -1; bool active = false; } leases_[2];
  struct Schedule {
    RadioProfileConfig config;
    uint32_t start = 0, end = 0, start_ms = 0, end_ms = 0;
    bool occupied = false, started = false;
  } schedules_[4];

  bool restoring_[2] = {};
  RadioProfileConfig saved_;
  uint16_t primary_preamble_ = 0;
  uint8_t cross_ = 0;
  CommonRadioPrefs* prefs_ = nullptr;
  Radio* radio_ = nullptr;
  RTCClock* rtc_ = nullptr;
  Dispatcher* dispatcher_ = nullptr;
  char remote_command_[160] = {};
  uint32_t remote_token_ = 0, accepted_ms_ = 0, accepted_epoch_ = 0, save_retry_ = 0;
  bool save_waiting_ = false;
  bool validating_ = false, replaying_ = false, reply_attached_ = false, reply_sent_ = false, reply_failed_ = false;
  bool apply(uint8_t profile, const RadioProfileConfig& config, bool temporary);
  void restore(uint8_t profile, uint32_t due);
  void queue(uint8_t profile, const RadioProfileConfig& config, bool temporary,
             bool save, uint32_t due, uint32_t end = 0);
  void cancelOverdue(uint8_t profile, uint32_t now, uint32_t epoch);
  void format(char* reply, size_t capacity, const RadioProfileConfig& config) const;
protected:
  void structure() override;
public:
  void begin(CommonRadioPrefs& prefs, Radio& radio, RTCClock& clock, Dispatcher* dispatcher = nullptr);
  // Starts wait for old outgoing work to drain. Remote starts additionally
  // require a successful transmission of their exact tagged CLI response.
  void loop(bool can_apply = true);
  bool handle(const char* command, char* reply, size_t capacity = 160, bool remote = false);
  void attachReply(Packet* packet);
  void onTransmitResult(uint32_t token, bool success);
  bool saveDue();
  bool needsService() const;
  bool awaitingReply() const { return remote_command_[0] != 0; }
  void primaryChanged();
  RadioProfileParams savedPrimary() const;
  const RadioProfileConfig& savedSecondary() const { return saved_; }
  bool pending() const { return pending_[0].active || pending_[1].active || awaitingReply(); }
};
}
