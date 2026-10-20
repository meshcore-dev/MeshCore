#include "RadioProfileCLI.h"
#include "CommonRadioPrefs.h"
#include <Dispatcher.h>
#include <Arduino.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mesh {
namespace {
constexpr uint32_t ReplyDelay = 2000;
constexpr uint32_t MaxMinutes = 10080;
bool due(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }
bool integer(const char* text, uint32_t& value) {
  if (!text || !*text) return false;
  value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9' || value > (UINT32_MAX - uint32_t(*text-'0')) / 10) return false;
    value = value * 10 + uint32_t(*text-'0');
  }
  return true;
}
bool decimal(const char* text, float& value) {
  if (!text || !*text) return false;
  // Decimal RF values only: no NaN, infinity, exponent, sign or hex aliases.
  unsigned dots = 0, digits = 0;
  for (const char* p = text; *p; ++p) {
    if (*p == '.') { if (++dots > 1) return false; }
    else if (*p >= '0' && *p <= '9') ++digits;
    else return false;
  }
  char* end;
  errno = 0;
  value = strtof(text, &end);
  return digits && !*end && errno != ERANGE && isfinite(value);
}
unsigned split(char* text, char* parts[], unsigned capacity) {
  unsigned count = 0;
  do {
    if (!*text || count == capacity) return 0;
    parts[count++] = text;
    char* comma = strchr(text, ',');
    if (!comma) break;
    *comma = 0; text = comma + 1;
  } while (true);
  return count;
}
bool preamble(const char* text, uint16_t& result) {
  if (!strcmp(text, "auto")) { result = 0; return true; }
  uint32_t value;
  if (!integer(text, value) || value > RadioProfiles::MaxPreamble || (value && value < 8)) return false;
  result = uint16_t(value); return true;
}
bool timestamp(const char* text, uint32_t epoch, uint32_t now, uint32_t& result, uint32_t& tick) {
  uint32_t value;
  if (!epoch || !integer(text + (*text == '+'), value)) return false;
  uint64_t absolute = *text == '+' ? uint64_t(epoch) + uint64_t(value) * 60 : value;
  if (absolute > UINT32_MAX || absolute <= epoch || (absolute - epoch) * 1000 > INT32_MAX) return false;
  result = uint32_t(absolute);
  tick = now + uint32_t((absolute - epoch) * 1000);
  return true;
}
const char* modeName(RadioProfileMode mode) {
  return mode == RadioProfileMode::RxTx ? "rxtx" : mode == RadioProfileMode::Rx ? "rx" : "off";
}
bool parseMode(const char* text, RadioProfileMode& mode) {
  if (!strcmp(text, "rx")) mode = RadioProfileMode::Rx;
  else if (!strcmp(text, "rxtx") || !strcmp(text, "rx&tx")) mode = RadioProfileMode::RxTx;
  else if (!strcmp(text, "off")) mode = RadioProfileMode::Off;
  else return false;
  return true;
}
}

void RadioProfileCLI::structure() {
  def("freq", saved_.params.freq); def("bw", saved_.params.bw);
  def("sf", saved_.params.sf); def("cr", saved_.params.cr);
  def("preamble", saved_.params.preamble);
  uint8_t mode = uint8_t(saved_.mode);
  def("mode", mode); saved_.mode = RadioProfileMode(mode);
  def("primary_pre", primary_preamble_); def("cross", cross_);
}

RadioProfileParams RadioProfileCLI::savedPrimary() const {
  RadioProfileParams params;
  if (prefs_) {
    params.freq = prefs_->getFreq(); params.bw = prefs_->getBandwidth();
    params.sf = prefs_->getSpreadFactor(); params.cr = prefs_->getCodingRate();
  }
  params.preamble = primary_preamble_;
  return params;
}

void RadioProfileCLI::begin(CommonRadioPrefs& prefs, Radio& radio, RTCClock& clock, Dispatcher* dispatcher) {
  dispatcher_ = dispatcher;
  prefs_ = &prefs; radio_ = &radio; rtc_ = &clock;
  if (!radio.profiles()) return;
  if (primary_preamble_ && (primary_preamble_ < 8 || primary_preamble_ > RadioProfiles::MaxPreamble)) primary_preamble_ = 0;
  if (uint8_t(saved_.mode) > uint8_t(RadioProfileMode::RxTx)
      || (saved_.mode != RadioProfileMode::Off && !radio.validateProfile(saved_.params))) saved_ = {};
  if (cross_ > uint8_t(RadioCrossMode::Off)) cross_ = uint8_t(RadioCrossMode::Off);
  radio.profiles()->cross = RadioCrossMode(cross_);
  RadioProfileConfig primary; primary.params = savedPrimary(); primary.mode = RadioProfileMode::RxTx;
  queue(0, primary, false, false, uint32_t(millis()));
  if (uint8_t(saved_.mode) <= uint8_t(RadioProfileMode::RxTx)
      && (saved_.mode == RadioProfileMode::Off || radio.validateProfile(saved_.params)))
    radio.profiles()->setSecondary(saved_, false);
}

void RadioProfileCLI::queue(uint8_t profile, const RadioProfileConfig& config, bool temporary,
                            bool save, uint32_t at, uint32_t end) {
  auto& pending = pending_[profile];
  pending.config = config; pending.due = at; pending.end = end;
  pending.temporary = temporary; pending.save = save; pending.retry = at; pending.active = true;
  pending.schedule = -1;
}

void RadioProfileCLI::restore(uint8_t profile, uint32_t at) {
  RadioProfileConfig config = saved_;
  if (!profile) { config.params = savedPrimary(); config.mode = RadioProfileMode::RxTx; }
  queue(profile, config, false, false, at);
}

bool RadioProfileCLI::apply(uint8_t profile, const RadioProfileConfig& config, bool temporary) {
  if (profile) { radio_->profiles()->setSecondary(config, temporary); return true; }
  return radio_->trySetPrimaryParams(config.params, temporary) == RadioParamApplyResult::APPLIED;
}

void RadioProfileCLI::attachReply(Packet* packet) {
  if (!remote_command_[0] || reply_attached_) return;
  reply_attached_ = true;
  if (packet && dispatcher_) dispatcher_->setTransmitToken(packet, remote_token_);
  else { remote_command_[0] = 0; reply_failed_ = true; }
}

void RadioProfileCLI::onTransmitResult(uint32_t token, bool success) {
  if (remote_command_[0] && token == remote_token_ && success) reply_sent_ = true;
}

void RadioProfileCLI::primaryChanged() {
  if (leases_[0].schedule >= 0) schedules_[leases_[0].schedule] = {};
  if (pending_[0].schedule >= 0) schedules_[pending_[0].schedule] = {};
  pending_[0].active = leases_[0].active = restoring_[0] = false;
  cancelOverdue(0, uint32_t(millis()), rtc_ ? rtc_->getCurrentTime() : 0);
}

bool RadioProfileCLI::needsService() const {
  if (pending() || restoring_[0] || restoring_[1] || leases_[0].active || leases_[1].active || isDirty()) return true;
  for (const auto& schedule : schedules_) if (schedule.occupied) return true;
  return false;
}

void RadioProfileCLI::cancelOverdue(uint8_t profile, uint32_t now, uint32_t epoch) {
  for (unsigned i = unsigned(profile) * 2; i < unsigned(profile) * 2 + 2; ++i) {
    auto& schedule = schedules_[i];
    if (schedule.occupied && !schedule.started && (due(now, schedule.start_ms) || epoch >= schedule.start))
      schedule = {};
  }
}

bool RadioProfileCLI::saveDue() {
  const uint32_t now = uint32_t(millis());
  if (!isDirty()) { save_waiting_ = false; return false; }
  if (save_waiting_ && !due(now, save_retry_)) return false;
  save_waiting_ = true; save_retry_ = now + 1000;
  return true;
}

void RadioProfileCLI::loop(bool can_apply) {
  if (!radio_ || !radio_->profiles()) return;
  const uint32_t now = uint32_t(millis());
  const uint32_t epoch = rtc_ ? rtc_->getCurrentTime() : 0;
  if (remote_command_[0] && reply_attached_ && can_apply) {
    char command[sizeof(remote_command_)]; strcpy(command, remote_command_);
    remote_command_[0] = 0;
    if (reply_sent_) {
      char ignored[160]; replaying_ = true;
      handle(command, ignored, sizeof(ignored));
      replaying_ = false;
    } else reply_failed_ = true;
  }
  // Expiry is independent of wall-clock changes and outgoing queue load.
  for (uint8_t profile = 0; profile < 2; ++profile) {
    if (leases_[profile].active && due(now, leases_[profile].end)) {
      if (leases_[profile].schedule >= 0) schedules_[leases_[profile].schedule] = {};
      leases_[profile].active = false;
      if (!profile) radio_->profiles()->setPrimary(savedPrimary(), false);
      restoring_[profile] = true;
    }
    auto& pending = pending_[profile];
    if (pending.active && pending.temporary && due(now, pending.end)) {
      pending.active = false;
      if (leases_[profile].active) { leases_[profile].active = false; restoring_[profile] = true; }
    }
  }
  // An expired generation must retire even if its replacement is waiting
  // for a response or a busy queue. Keep the replacement in its own slot.
  for (uint8_t profile = 0; profile < 2; ++profile) {
    if (!restoring_[profile]) continue;
    RadioProfileConfig baseline = saved_;
    if (!profile) { baseline.params = savedPrimary(); baseline.mode = RadioProfileMode::RxTx; }
    if (apply(profile, baseline, false)) restoring_[profile] = false;
  }
  // Catch up in scheduled start order so a late loop cannot let an older
  // temporary start overwrite a newer permanent change. Equal starts apply
  // the permanent baseline first, then its temporary overlay.
  for (uint8_t profile = 0; profile < 2; ++profile) {
    int selected = -1;
    for (unsigned i = unsigned(profile) * 2; i < unsigned(profile) * 2 + 2; ++i) {
      auto& schedule = schedules_[i];
      const bool temporary = i & 1;
      if (!schedule.occupied) continue;
      if (temporary && !schedule.started && (due(now, schedule.end_ms) || epoch >= schedule.end)) {
        schedule = {}; continue;
      }
      if (schedule.started || pending_[profile].active || !can_apply
          || (!due(now, schedule.start_ms) && epoch < schedule.start)) continue;
      if (selected < 0 || schedule.start < schedules_[selected].start) selected = int(i);
    }
    if (selected >= 0) {
      auto& schedule = schedules_[selected];
      const bool temporary = selected & 1;
      queue(profile, schedule.config, temporary, !temporary, now, schedule.end_ms);
      pending_[profile].schedule = int8_t(selected);
      if (temporary) schedule.started = true;
      else schedule = {};
    }
  }
  for (uint8_t profile = 0; profile < 2; ++profile) {
    auto& pending = pending_[profile];
    if (restoring_[profile] || !pending.active || !due(now, pending.due) || !due(now, pending.retry)) continue;
    if (!can_apply) continue;
    if (!apply(profile, pending.config, pending.temporary)) {
      pending.retry = now + 1000; continue;
    }
    if (leases_[profile].active && leases_[profile].schedule >= 0
        && leases_[profile].schedule != pending.schedule)
      schedules_[leases_[profile].schedule] = {};
    leases_[profile].active = pending.temporary;
    leases_[profile].end = pending.end;
    leases_[profile].schedule = pending.schedule;
    if (pending.save) {
      if (profile) saved_ = pending.config;
      else {
        prefs_->setFreq(pending.config.params.freq); prefs_->setBandwidth(pending.config.params.bw);
        prefs_->setSpreadFactor(pending.config.params.sf); prefs_->setCodingRate(pending.config.params.cr);
        primary_preamble_ = pending.config.params.preamble;
      }
      markDirty();
    }
    pending.active = false;
  }
}

void RadioProfileCLI::format(char* reply, size_t capacity, const RadioProfileConfig& config) const {
  if (config.mode == RadioProfileMode::Off) { snprintf(reply, capacity, "> off"); return; }
  snprintf(reply, capacity, "> %.6f,%.3f,%u,%u,%s,preamble=%u", double(config.params.freq),
           double(config.params.bw), unsigned(config.params.sf), unsigned(config.params.cr),
           modeName(config.mode), unsigned(config.params.preamble));
}

bool RadioProfileCLI::handle(const char* command, char* reply, size_t capacity, bool remote) {
  enum Verb { Get, Set, Delete } verb;
  const char* text;
  if (!strncmp(command, "get ", 4)) { verb = Get; text = command + 4; }
  else if (!strncmp(command, "set ", 4)) { verb = Set; text = command + 4; }
  else if (!strncmp(command, "del ", 4)) { verb = Delete; text = command + 4; }
  else if (!strncmp(command, "tempradio ", 10)) { verb = Set; text = command; }
  else return false;
  char key[24];
  const char* space = strchr(text, ' ');
  const size_t length = space ? size_t(space - text) : strlen(text);
  if (length >= sizeof(key)) return false;
  memcpy(key, text, length); key[length] = 0;
  const char* args = space ? space + 1 : "";
  if (!strcmp(key, "radioat2")) strcpy(key, "radio2at");
  if (!strcmp(key, "tempradioat2")) strcpy(key, "tempradio2at");
  int schedule_index = !strcmp(key, "radioat") ? 0 : !strcmp(key, "tempradioat") ? 1
      : !strcmp(key, "radio2at") ? 2 : !strcmp(key, "tempradio2at") ? 3 : -1;
  const bool secondary = !strcmp(key, "radio2") || !strcmp(key, "tempradio2") || schedule_index >= 2;
  const bool temporary = !strcmp(key, "tempradio") || !strcmp(key, "tempradio2")
      || (schedule_index >= 0 && (schedule_index & 1));
  const bool primary = !strcmp(key, "radio");
  const bool cross = !strcmp(key, "radio2.cross");
  const bool status = !strcmp(key, "radio2.status");
  const bool timing = !strcmp(key, "radio2.timing");
  if (!primary && !secondary && !temporary && schedule_index < 0 && !cross && !status && !timing) return false;
  auto error = [&](const char* message) { snprintf(reply, capacity, "Error: %s", message); return true; };
  if (!prefs_ || !radio_ || !radio_->profiles()) {
    if (primary || !strncmp(command, "tempradio ", 10)) return false;
    return error("radio profiles unavailable");
  }
  const uint8_t profile = secondary ? 1 : 0;
  if (verb != Get && !validating_ && !replaying_) {
    if (remote_command_[0]) return error("radio response still pending");
    if (remote) {
      if (!dispatcher_ || strlen(command) >= sizeof(remote_command_)) return error("remote radio command unavailable");
      accepted_ms_ = uint32_t(millis()); accepted_epoch_ = rtc_ ? rtc_->getCurrentTime() : 0;
      validating_ = true;
      const bool handled = handle(command, reply, capacity);
      validating_ = false;
      if (handled && !strncmp(reply, "OK", 2)) {
        strcpy(remote_command_, command); remote_token_ = dispatcher_->reserveTransmitToken();
        reply_attached_ = reply_sent_ = reply_failed_ = false;
      }
      return handled;
    }
  }
  const uint32_t now = (validating_ || replaying_) ? accepted_ms_ : uint32_t(millis());
  if (cross) {
    if (verb == Get && !*args) snprintf(reply, capacity, "> %s", cross_ == 1 ? "on" : cross_ == 2 ? "off" : "auto");
    else if (verb == Set && (!strcmp(args, "auto") || !strcmp(args, "on") || !strcmp(args, "off"))) {
      if (validating_) { snprintf(reply, capacity, "OK"); return true; }
      cross_ = !strcmp(args, "on") ? 1 : !strcmp(args, "off") ? 2 : 0;
      radio_->profiles()->cross = RadioCrossMode(cross_); markDirty(); snprintf(reply, capacity, "OK");
    } else return error("use radio2.cross auto|on|off");
    return true;
  }
  if (status || timing) {
    if (verb != Get || *args) return error("read-only");
    const auto& p = *radio_->profiles();
    if (timing) snprintf(reply, capacity, "> switch=%luus preamble=%u,%u", (unsigned long)p.switchBudgetUs(),
                         unsigned(radio_->profilePreamble(0)), unsigned(radio_->profilePreamble(1)));
    else snprintf(reply, capacity, "> %s; switches=%lu errors=%lu max=%luus pending=%u reply_failed=%u", modeName(p.secondary.mode),
                  (unsigned long)p.switches, (unsigned long)p.switch_failures, (unsigned long)p.longest_switch_us,
                  unsigned(pending()), unsigned(reply_failed_));
    return true;
  }
  if (schedule_index >= 0 && (verb == Get || verb == Delete)) {
    if (*args && strcmp(args, "1") && strcmp(args, "all")) return error("one schedule slot");
    auto& schedule = schedules_[schedule_index];
    if (verb == Delete) {
      if (validating_) { snprintf(reply, capacity, "OK"); return true; }
      schedule = {};
      if (pending_[profile].active && pending_[profile].schedule == schedule_index) pending_[profile].active = false;
      if (leases_[profile].active && leases_[profile].schedule == schedule_index) {
        restore(profile, now + ReplyDelay);
      }
      snprintf(reply, capacity, "OK");
    }
    else if (!schedule.occupied) snprintf(reply, capacity, "> off");
    else snprintf(reply, capacity, "> start=%lu end=%lu %.6f,%.3f,%u,%u,%s,preamble=%u",
        (unsigned long)schedule.start, (unsigned long)schedule.end, double(schedule.config.params.freq),
        double(schedule.config.params.bw), unsigned(schedule.config.params.sf), unsigned(schedule.config.params.cr),
        modeName(schedule.config.mode), unsigned(schedule.config.params.preamble));
    return true;
  }
  if (verb == Get) {
    if (*args) return error("unexpected argument");
    if (primary) {
      auto p = savedPrimary();
      snprintf(reply, capacity, "> %.6f,%.3f,%u,%u,preamble=%u", double(p.freq), double(p.bw),
               unsigned(p.sf), unsigned(p.cr), unsigned(p.preamble));
    } else if (!temporary) format(reply, capacity, saved_);
    else if (pending_[profile].active && pending_[profile].temporary) format(reply, capacity, pending_[profile].config);
    else if (leases_[profile].active) {
      RadioProfileConfig active; active.mode = profile ? radio_->profiles()->secondary.mode : RadioProfileMode::RxTx;
      active.params = profile ? radio_->profiles()->secondary.params : radio_->profiles()->primary;
      format(reply, capacity, active);
    } else snprintf(reply, capacity, "> off");
    return true;
  }
  if (verb != Set || !*args || strlen(args) >= 128) return error("invalid radio command");
  if (!strcmp(args, "off") && schedule_index < 0 && !primary) {
    if (validating_) { snprintf(reply, capacity, "OK - restore pending"); return true; }
    if (!temporary) { saved_ = {}; markDirty(); }
    schedules_[profile * 2 + 1] = {};
    if (!temporary) schedules_[profile * 2] = {};
    restore(profile, now + ReplyDelay);
    snprintf(reply, capacity, "OK - restore pending"); return true;
  }
  char input[128]; strcpy(input, args);
  char* parts[9]; const unsigned count = split(input, parts, 9);
  const unsigned expected = secondary ? 5 : 4;
  const unsigned extra = schedule_index >= 0 ? (temporary ? 2 : 1) : temporary ? 1 : 0;
  RadioProfileConfig config; config.mode = RadioProfileMode::RxTx;
  uint32_t sf, cr;
  if ((count != expected + extra && count != expected + extra + 1)
      || !decimal(parts[0], config.params.freq) || !decimal(parts[1], config.params.bw)
      || !integer(parts[2], sf) || sf > UINT8_MAX || !integer(parts[3], cr) || cr > UINT8_MAX
      || (secondary && !parseMode(parts[4], config.mode))) return error("invalid radio tuple");
  config.params.sf = uint8_t(sf); config.params.cr = uint8_t(cr);
  if (count > expected + extra && !preamble(parts[expected + extra], config.params.preamble))
    return error("invalid preamble");
  if (!radio_->validateProfile(config.params)) return error("unsupported radio tuple");
  auto preview = *radio_->profiles();
  if (secondary) preview.setSecondary(config, temporary); else preview.setPrimary(config.params, temporary);
  if (!preview.automaticPreambleFits()) return error("automatic preamble exceeds limit");
  if (schedule_index >= 0) {
    Schedule next; next.config = config;
    const uint32_t epoch = (validating_ || replaying_) ? accepted_epoch_ : rtc_ ? rtc_->getCurrentTime() : 0;
    if (!timestamp(parts[expected], epoch, now, next.start, next.start_ms)
        || (temporary && (!timestamp(parts[expected+1], epoch, now, next.end, next.end_ms) || next.end <= next.start)))
      return error("future UTC or +minutes required, within 24 days");
    if (temporary && (leases_[profile].active || (pending_[profile].active && pending_[profile].temporary)))
      return error("end current temporary session first");
    if (schedules_[schedule_index].started) return error("cancel active schedule first");
    if (validating_) { snprintf(reply, capacity, "OK - schedule queued (one slot)"); return true; }
    next.occupied = true; schedules_[schedule_index] = next;
    snprintf(reply, capacity, "OK - schedule queued (one slot)"); return true;
  }
  if (temporary) {
    uint32_t minutes;
    if (!integer(parts[expected], minutes) || !minutes || minutes > MaxMinutes) return error("minutes must be 1..10080");
    if (schedules_[profile*2+1].occupied) return error("clear temporary schedule first");
    if (validating_) { snprintf(reply, capacity, "OK - temporary for %lu min; apply pending", (unsigned long)minutes); return true; }
    cancelOverdue(profile, now, (replaying_ ? accepted_epoch_ : rtc_->getCurrentTime()));
    queue(profile, config, true, false, now + ReplyDelay, now + ReplyDelay + minutes * 60000UL);
    snprintf(reply, capacity, "OK - temporary for %lu min; apply pending", (unsigned long)minutes);
  } else if (primary) {
    if (validating_) { snprintf(reply, capacity, "OK - reboot to apply"); return true; }
    cancelOverdue(profile, now, (replaying_ ? accepted_epoch_ : rtc_->getCurrentTime()));
    prefs_->setFreq(config.params.freq); prefs_->setBandwidth(config.params.bw);
    prefs_->setSpreadFactor(config.params.sf); prefs_->setCodingRate(config.params.cr);
    primary_preamble_ = config.params.preamble; markDirty();
    snprintf(reply, capacity, "OK - reboot to apply");
  } else {
    if (validating_) { snprintf(reply, capacity, "OK - saved; apply pending"); return true; }
    cancelOverdue(profile, now, (replaying_ ? accepted_epoch_ : rtc_->getCurrentTime()));
    saved_ = config; markDirty();
    queue(1, config, false, false, now + ReplyDelay);
    snprintf(reply, capacity, "OK - saved; apply pending");
  }
  return true;
}
}
