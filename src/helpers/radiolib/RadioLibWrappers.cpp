#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"
#include "RadioInterruptEvents.h"
#include "RadioProfileChip.h"

#define STATE_IDLE 0
#define STATE_RX 1
#define STATE_TX_WAIT 3
#define STATE_TX_DONE 4
#define NUM_NOISE_FLOOR_SAMPLES 64
#define SAMPLING_THRESHOLD 14

// The firmware has one physical transceiver. ISR publication is separate from
// loop-owned mode state: an interrupt during a mode change must remain pending.
static RadioInterruptEvents radio_interrupt;
static
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag() { radio_interrupt.record(); }

// Extend the 24-bit RTC counter on each loop poll. A valid explicit preamble
// can require a visit longer than its 512-second hardware wrap period.
#if defined(NRF52_PLATFORM)
static uint32_t profile_clock_raw = 0;
static uint32_t profile_clock_ticks = 0;
#endif

static void syncProfileClock(bool dual_profile) {
#if defined(NRF52_PLATFORM)
  static bool active = false;
  if (active == dual_profile) return;
  if (dual_profile) {
    NRF_RTC2->TASKS_STOP = 1;
    NRF_RTC2->PRESCALER = 0;
    // Reset the software origin against the retained counter. A delayed
    // TASKS_CLEAR must not be mistaken for a hardware counter wrap.
    profile_clock_raw = NRF_RTC2->COUNTER & 0x00ffffffUL;
    profile_clock_ticks = 0;
    NRF_RTC2->TASKS_START = 1;
  } else {
    NRF_RTC2->TASKS_STOP = 1;
  }
  active = dual_profile;
#else
  (void) dual_profile;
#endif
}

static uint32_t profileTimestamp(bool dual_profile) {
  syncProfileClock(dual_profile);
#if defined(NRF52_PLATFORM)
  if (dual_profile) {
    const uint32_t raw = NRF_RTC2->COUNTER & 0x00ffffffUL;
    profile_clock_ticks += (raw - profile_clock_raw) & 0x00ffffffUL;
    profile_clock_raw = raw;
    return profile_clock_ticks;
  }
#endif
  return micros();
}

static uint32_t profileElapsedUs(uint32_t since, uint32_t now, bool dual_profile) {
#if defined(NRF52_PLATFORM)
  if (!dual_profile) return now - since;
  // Unsigned subtraction also handles wrap of the extended 32-bit tick clock.
  const uint32_t ticks = now - since;
  const uint64_t elapsed = (uint64_t(ticks) * 15625 + 511) >> 9;
  return elapsed > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(elapsed);
#else
  (void) dual_profile;
  return now - since;
#endif
}


void RadioLibWrapper::begin() {
  radio_interrupt.claim();
  _radio->setPacketReceivedAction(setFlag);
  _preamble_sf = getSpreadingFactor();
  _physical_preamble = preambleLengthForSF(_preamble_sf);
  _radio->setPreambleLength(_physical_preamble);
  _state = STATE_IDLE;
  _noise_floor = _threshold = 0;
  _cad_enabled = false;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
  _physical_params.freq = LORA_FREQ;
  _physical_params.bw = LORA_BW;
  _physical_params.sf = _preamble_sf;
#ifdef LORA_CR
  _physical_params.cr = LORA_CR;
#else
  _physical_params.cr = 5;
#endif
  _profiles.primary = _physical_params;
  _params_valid = true;
  _profile_generation = _profiles.generation[0];
  _last_rx_generation = _profile_generation;
  _profile_visit_stamp = profileTimestamp(false);
  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {
    _state = STATE_RX;
    setFlag();
  }
}

uint32_t RadioLibWrapper::getRngSeed() { return _radio->random(0x7FFFFFFF); }

void RadioLibWrapper::setTxPower(int8_t dbm) {
#if defined(USE_LR2021)
  idle();
#endif
  _radio->setOutputPower(dbm);
}

void RadioLibWrapper::idle() {
  _radio->standby();
  _state = STATE_IDLE;
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void RadioLibWrapper::doResetAGC() { _radio->sleep(); }

void RadioLibWrapper::resetAGC() {
  if (_state == STATE_TX_WAIT || _state == STATE_TX_DONE || radio_interrupt.pending()
      || isChipBusy() || isReceivingPacket()) return;
  doResetAGC();
  _state = STATE_IDLE;
  _noise_floor = 0;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

void RadioLibWrapper::saveNoiseFloor(uint8_t profile) {
  _profile_noise_floor[profile] = _noise_floor;
  _profile_floor_samples[profile] = _num_floor_samples;
  _profile_floor_sum[profile] = _floor_sample_sum;
}
void RadioLibWrapper::loadNoiseFloor(uint8_t profile) {
  _noise_floor = _profile_noise_floor[profile];
  _num_floor_samples = _profile_floor_samples[profile];
  _floor_sample_sum = _profile_floor_sum[profile];
}

void RadioLibWrapper::loop() {
  if (_state == STATE_RX && !radio_interrupt.pending() && !isChipBusy()
      && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!isReceivingPacket()) {
      int rssi = getCurrentRSSI();
      if (rssi < _noise_floor + SAMPLING_THRESHOLD) {
        ++_num_floor_samples;
        _floor_sample_sum += rssi;
      }
    }
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && _floor_sample_sum != 0) {
    _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (_noise_floor < -120) _noise_floor = -120;
    _floor_sample_sum = 0;
  }
  serviceProfiles();
}

bool RadioLibWrapper::startRecv() {
#if defined(USE_LR2021)
  _radio->standby();
#endif
  const int16_t rc = _radio->startReceive();
  _state = rc == RADIOLIB_ERR_NONE ? STATE_RX : STATE_IDLE;
  if (rc != RADIOLIB_ERR_NONE) MESH_DEBUG_PRINTLN("RadioLibWrapper: startReceive(%d)", rc);
  return rc == RADIOLIB_ERR_NONE;
}

bool RadioLibWrapper::isInRecvMode() const { return _state == STATE_RX; }

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  if (_state == STATE_TX_WAIT || _state == STATE_TX_DONE || _profile_refresh_required) return 0;
  int len = 0;
  if (radio_interrupt.claim()) {
    len = _radio->getPacketLength();
    if (len > 0) {
      if (len > sz) len = sz;
      const int16_t rc = _radio->readData(bytes, len);
      if (rc != RADIOLIB_ERR_NONE) {
        ++n_recv_errors;
        len = 0;
      } else {
        ++n_recv;
        _last_rx_profile = _active_profile;
        _last_rx_generation = _profile_generation;
        _last_rssi = _radio->getRSSI();
        _last_snr = _radio->getSNR();
      }
    }
#if defined(USE_LR2021)
    _state = STATE_RX;
#else
    _state = STATE_IDLE;
#endif
  }
  if (_state != STATE_RX && !radio_interrupt.pending()) startRecv();
  return len;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len) { return _radio->getTimeOnAir(len) / 1000; }

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  if (_state == STATE_TX_WAIT || _state == STATE_TX_DONE || radio_interrupt.pending()
      || isChipBusy() || isReceivingPacket() || _profile_refresh_required) return false;
  _board->onBeforeTransmit();
  const int16_t rc = _radio->startTransmit((uint8_t*)bytes, len);
  if (rc == RADIOLIB_ERR_NONE) {
    _state = STATE_TX_WAIT;
    return true;
  }
  idle();
  _board->onAfterTransmit();
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  if (_state == STATE_TX_WAIT && radio_interrupt.claim()) {
    _state = STATE_TX_DONE;
    ++n_sent;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  _radio->finishTransmit();
  _board->onAfterTransmit();
  _state = STATE_IDLE;
  _profile_visit_stamp = profileTimestamp(_profiles.enabled());
}

int16_t RadioLibWrapper::performChannelScan() { return _radio->scanChannel(); }

bool RadioLibWrapper::isChannelActive() {
  if (radio_interrupt.pending() || isChipBusy() || isReceivingPacket()) return true;
  if (_threshold != 0 && getCurrentRSSI() > _noise_floor + _threshold) return true;
  if (_cad_enabled) {
    const int16_t result = performChannelScan();
    radio_interrupt.claim(); // This completion belongs to the synchronous CAD.
    _state = STATE_IDLE;
    startRecv();
    return result != RADIOLIB_CHANNEL_FREE;
  }
  return false;
}

float RadioLibWrapper::getLastRSSI() const { return _last_rssi; }
float RadioLibWrapper::getLastSNR() const { return _last_snr; }

// 0/1 = safely stopped (whether to resume RX), 2 = busy, 3 = command failure.
uint8_t RadioLibWrapper::beginReconfigure() {
  if (_state == STATE_TX_WAIT || _state == STATE_TX_DONE || isChipBusy()
      || (!_profile_refresh_required && (radio_interrupt.pending() || isReceivingPacket()))) return 2;
  // After a failed rollback, no FIFO contents have a trustworthy profile.
  // Discard those events and rebuild RX before allowing delivery or TX.
  if (_profile_refresh_required) radio_interrupt.claim();
  const bool resume = _state == STATE_RX || _profile_refresh_required;
  if (resume) {
    if (_radio->standby() != RADIOLIB_ERR_NONE) return 3;
    if (_profile_refresh_required) radio_interrupt.claim();
    // A completed packet on a known tuple retains its old FIFO and profile.
    else if (radio_interrupt.pending()) return 2;
  }
  _state = STATE_IDLE;
  return resume;
}

mesh::RadioParamApplyResult RadioLibWrapper::tuneProfile(uint8_t profile) {
  using Result = mesh::RadioParamApplyResult;
  if (profile > 1 || (profile == 1 && !_profiles.enabled())) return Result::FAILED;
  const auto p = _profiles.params(profile);
  const uint16_t preamble = profilePreamble(profile);
  if (!validateProfile(p) || !mesh::RadioProfiles::safePreamble(p, preamble)) return Result::FAILED;
  if (!_profile_refresh_required && _active_profile == profile && _params_valid
      && _profile_generation == _profiles.generation[profile]
      && _physical_params == p && _physical_preamble == preamble
      && _physical_dual == _profiles.enabled()) return Result::APPLIED;
  const bool dual = _profiles.enabled();
  const uint32_t started = profileTimestamp(dual);
  beginProfileRetune(_state == STATE_RX);
  const uint8_t resume = beginReconfigure();
  if (resume > 1) {
    endProfileRetune(resume == 2);
    if (resume == 3) { _state = STATE_IDLE; _profile_refresh_required = true; }
    return resume == 2 ? Result::BUSY : Result::FAILED;
  }
  const auto previous = _physical_params;
  const uint16_t previous_preamble = _physical_preamble;
  const uint8_t previous_profile = _active_profile;
  const uint32_t previous_generation = _profile_generation;
  const bool had_previous = _params_valid;
  _physical_preamble = preamble;
  bool applied = applyParams(p.freq, p.bw, p.sf, p.cr);
  if (applied && resume) applied = startRecv();
  endProfileRetune(applied);
  if (applied) {
    saveNoiseFloor(previous_profile);
    _physical_params = p;
    _physical_dual = dual;
    _params_valid = true;
    _active_profile = profile;
    _profile_generation = _profiles.generation[profile];
    _profile_refresh_required = false;
    if (profile != previous_profile) loadNoiseFloor(profile);
    ++_profiles.switches;
  } else {
    ++_profiles.switch_failures;
    _physical_preamble = previous_preamble;
    // Full setup after revoking fast ownership repairs partial command writes.
    bool restored = had_previous && _radio->standby() == RADIOLIB_ERR_NONE
        && applyParams(previous.freq, previous.bw, previous.sf, previous.cr);
    if (restored && resume) restored = startRecv();
    _physical_params = previous;
    _active_profile = previous_profile;
    _profile_generation = previous_generation;
    _params_valid = had_previous;
    _profile_refresh_required = !restored;
  }
  _profile_visit_stamp = profileTimestamp(dual);
  const uint32_t elapsed = profileElapsedUs(started, _profile_visit_stamp, dual);
  if (elapsed > _profiles.longest_switch_us) _profiles.longest_switch_us = elapsed;
  if (applied && resume && previous_profile != profile)
    _profiles.sampleSwitch(previous_profile, profile, elapsed);
  return applied ? Result::APPLIED : Result::FAILED;
}

void RadioLibWrapper::serviceProfiles() {
  const bool dual = _profiles.enabled();
  // Poll even during RX/TX deferrals so long packet activity cannot hide an
  // RTC hardware wrap from the software extension.
  const uint32_t now = profileTimestamp(dual);
  if (_state == STATE_TX_WAIT || _state == STATE_TX_DONE || isChipBusy()
      || (!_profile_refresh_required && (radio_interrupt.pending() || isReceivingPacket()))) return;
  const bool changed = _profile_scan_generation[0] != _profiles.generation[0]
      || _profile_scan_generation[1] != _profiles.generation[1];
  setProfileStandbyWarm(dual);
  uint8_t target = _active_profile;
  if (!dual) target = 0;
  else if (changed) target = _profiles.slowerProfile();
  else if (profileElapsedUs(_profile_visit_stamp, now, dual)
      >= _profiles.listenUs(_active_profile, profilePreamble(_profiles.slowerProfile())))
    target ^= 1;
  const auto result = tuneProfile(target);
  if (changed && result == mesh::RadioParamApplyResult::APPLIED) {
    _profile_scan_generation[0] = _profiles.generation[0];
    _profile_scan_generation[1] = _profiles.generation[1];
    // New profile settings must collect their own interference baseline.
    _profile_noise_floor[0] = _profile_noise_floor[1] = 0;
    _profile_floor_samples[0] = _profile_floor_samples[1] = 0;
    _profile_floor_sum[0] = _profile_floor_sum[1] = 0;
    _noise_floor = 0;
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

mesh::RadioParamApplyResult RadioLibWrapper::prepareTransmitProfile(uint8_t profile) {
  if (!_profiles.canTransmit(profile)) return mesh::RadioParamApplyResult::FAILED;
  return tuneProfile(profile);
}

uint32_t RadioLibWrapper::getProfileAirtime(uint8_t profile, int len, uint8_t cr) {
  if (profile > 1 || len < 0 || len > MAX_TRANS_UNIT) return 0;
  const auto& p = _profiles.params(profile);
  if (!validateProfile(p)) return 0;
  const double symbol = mesh::RadioProfiles::symbolUs(p);
  if (cr < 5 || cr > 8) cr = p.cr;
  const int de = symbol >= 16000
#if MESH_RADIO_PROFILE_CHIP == 1121
      // Match pinned RadioLib's high-band SX128x compatibility rule.
      || (p.freq > 1000 && p.sf > 10)
#endif
      ? 1 : 0;
  const double coded = ceil((8.0 * len - 4 * p.sf + (p.sf <= 6 ? 20 : 28) + 16)
      / (4 * (p.sf - 2 * de)));
  const double payload = 8 + (coded > 0 ? coded * cr : 0);
  const double airtime = ceil((profilePreamble(profile) + (p.sf <= 6 ? 6.25 : 4.25)
      + payload) * symbol / 1000.0);
  const uint32_t rounded = airtime > UINT32_MAX ? UINT32_MAX : uint32_t(airtime);
  return rounded < minimumAirtimeMillis() ? minimumAirtimeMillis() : rounded;
}

mesh::RadioParamApplyResult RadioLibWrapper::trySetPrimaryParams(const mesh::RadioProfileParams& p,
    bool temporary, const uint32_t* timings) {
  (void)timings; // Upstream uses continuous RX; no RX duty-cycle setting exists.
  auto preview = _profiles;
  preview.setPrimary(p, temporary);
  if (!validateProfile(p) || !preview.automaticPreambleFits())
    return mesh::RadioParamApplyResult::FAILED;
  const auto previous = _profiles;
  _profiles = preview;
  const auto result = tuneProfile(0);
  if (result != mesh::RadioParamApplyResult::APPLIED) _profiles = previous;
  return result;
}

mesh::RadioParamApplyResult RadioLibWrapper::trySetParams(float freq, float bw, uint8_t sf,
    uint8_t cr, const uint32_t* timings) {
  auto p = _profiles.primary;
  p.freq = freq; p.bw = bw; p.sf = sf; p.cr = cr;
  return trySetPrimaryParams(p, _profiles.primary_temporary, timings);
}

bool RadioLibWrapper::validateProfile(const mesh::RadioProfileParams& p) const {
  if (!mesh::RadioProfiles::valid(p)) return false;
#if MESH_RADIO_PROFILE_CHIP != 1110 && MESH_RADIO_PROFILE_CHIP != 1121 && MESH_RADIO_PROFILE_CHIP != 2021 && MESH_RADIO_PROFILE_CHIP != 68 && MESH_RADIO_PROFILE_CHIP != 1272
  static constexpr float bandwidths[] = {7.8f,10.4f,15.6f,20.8f,31.25f,41.7f,62.5f,125,250,500};
  bool supported = false;
  for (float bw : bandwidths) if (fabsf(p.bw - bw) < 0.01f) supported = true;
  if (!supported) return false;
#endif
#if MESH_RADIO_PROFILE_CHIP == 1276
  return p.freq >= 137 && p.freq <= 1020 && p.sf >= 7 && p.bw <= 500;
#elif MESH_RADIO_PROFILE_CHIP == 1272
  return p.freq >= 860 && p.freq <= 1020 && p.sf >= 7
      && (p.bw == 125 || p.bw == 250 || p.bw == 500);
#elif MESH_RADIO_PROFILE_CHIP == 68
  return p.freq >= 150 && p.freq <= 960
      && ((p.bw == 125 && p.sf <= 9) || (p.bw == 250 && p.sf <= 10)
          || (p.bw == 500 && p.sf <= 11));
#elif MESH_RADIO_PROFILE_CHIP == 2021
  return ((p.freq >= 150 && p.freq <= 1090) || (p.freq >= 1900 && p.freq <= 2200)
      || (p.freq >= 2400 && p.freq <= 2500)) && p.bw >= 31.25 && p.bw <= 1000;
#elif MESH_RADIO_PROFILE_CHIP == 1110
  return p.freq >= 150 && p.freq <= 960
      && (p.bw == 62.5 || p.bw == 125 || p.bw == 250 || p.bw == 500);
#elif MESH_RADIO_PROFILE_CHIP == 1121
  return ((p.freq >= 150 && p.freq <= 960) || (p.freq >= 1900 && p.freq <= 2200)
      || (p.freq >= 2400 && p.freq <= 2500))
      && (p.bw == 62.5 || p.bw == 125 || p.bw == 250 || p.bw == 500
          || (p.freq >= 1900 && (p.bw == 203.125f || p.bw == 406.25f || p.bw == 812.5f)));
#elif MESH_RADIO_PROFILE_CHIP == 1268
  return p.freq >= 410 && p.freq <= 810 && p.bw <= 500;
#else
  return p.freq >= 150 && p.freq <= 960 && p.bw <= 500;
#endif
}

// Approximate SNR threshold per SF for successful reception (based on Semtech datasheets)
static float snr_threshold[] = {
    -7.5,  // SF7 needs at least -7.5 dB SNR
    -10,   // SF8 needs at least -10 dB SNR
    -12.5, // SF9 needs at least -12.5 dB SNR
    -15,  // SF10 needs at least -15 dB SNR
    -17.5,// SF11 needs at least -17.5 dB SNR
    -20   // SF12 needs at least -20 dB SNR
};

float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7 || sf > 12) return 0.0f;

  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}

PacketMillis RadioLibWrapper::calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint16_t preambleSymbols) {
  // based on RadioLib's calculateTimeOnAir()
  uint32_t tsym_us = ((uint32_t)10000 << sf) / (bw * 10);
  uint32_t sfCoeff1_x4 = (sf == 5 || sf == 6) ? 25 : 17; // 6.25 : 4.25, semtech magic numbers to account for sync word + sfd

  // preamble + syncword + sfd + header
  uint32_t preamble_us = (((preambleSymbols + 8) * 4 + sfCoeff1_x4) * tsym_us) / 4;

  // airtime for max packet at current radio settings
  uint32_t total_us   = _radio->getTimeOnAir(MAX_TRANS_UNIT);
  // airtime for payload only (no preamble, header or SOF)
  uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us : (preamble_us < 4000000UL ? 4000000UL - preamble_us : 0); // fallback to 4 secs at worst case
  // rescale payload_us for max possible CR
  if (cr >= 5 && cr < 8) { payload_us = (uint64_t(payload_us) * 8) / cr; }

  return PacketMillis {(preamble_us + 999) / 1000, (payload_us + 999) / 1000};
}
