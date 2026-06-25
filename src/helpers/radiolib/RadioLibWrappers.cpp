
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#include <algorithm>

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define RSSI_CARRIER_SENSE_SAMPLES  5
#define RSSI_CARRIER_SENSE_REQUIRED 3
#define MIN_NOISE_FLOOR_SAMPLE -130
#define MAX_NOISE_FLOOR_SAMPLE -80
#define LOW_BOUND_REJECT_JUMP_DB 14
#define HIGH_BOUND_REJECT_JUMP_DB 14

static volatile uint8_t state = STATE_IDLE;

static bool isTrustedNoiseFloorValue(int16_t noise_floor) {
  return noise_floor != 0;
}

static bool elapsedAtLeast(unsigned long now, unsigned long started_at, uint32_t interval_ms) {
  return (uint32_t)(now - started_at) >= interval_ms;
}

static uint16_t addStatCounter(uint16_t value, uint16_t increment) {
  return (uint16_t)mesh::cappedStatCounter((uint32_t)value + increment);
}

// this function is called when a complete packet
// is transmitted by the module
static 
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  // we sent a packet, set the flag
  state |= STATE_INT_READY;
}

void RadioLibWrapper::resetNoiseFloorSamples() {
  _num_floor_samples = 0;
  _floor_sample_min = 0;
  _floor_sample_median = 0;
  _floor_sample_max = 0;
  _noise_floor_batch_started_at = 0;
  _last_noise_floor_sample_at = 0;
  _noise_floor_batch_active = false;
  _has_last_noise_floor_sample = false;
}

void RadioLibWrapper::resetNoiseFloorBatch() {
  resetNoiseFloorSamples();
  _floor_rejected_low_bound = 0;
  _floor_rejected_high_bound = 0;
}

void RadioLibWrapper::begin() {
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt
  _preamble_sf = getSpreadingFactor();
  _radio->setPreambleLength(preambleLengthForSF(_preamble_sf)); // longer preamble for lower SF improves reliability
  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {  // received a LoRa packet (while in deep sleep)
    setFlag(); // LoRa packet is already received
  }

  _noise_floor = 0;
  _threshold = 0;
  _cad_enabled = false;

  // Start a fresh batch of idle RSSI samples for noise-floor calibration.
  resetNoiseFloorBatch();
}

uint32_t RadioLibWrapper::getRngSeed() {
  return _radio->random(0x7FFFFFFF);
}

void RadioLibWrapper::setTxPower(int8_t dbm) {
  _radio->setOutputPower(dbm);
}

void RadioLibWrapper::idle() {
  _radio->standby();
  state = STATE_IDLE;   // need another startReceive()
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES ||
      (_num_floor_samples == 0 &&
       (_floor_rejected_low_bound > 0 || _floor_rejected_high_bound > 0))) {
    // Start a new stats window after a completed batch, or after a
    // rejected-only interval. Without the second case, a device sitting on the
    // low RSSI reporting bound can accumulate a stale count for the whole
    // uptime even though no calibration batch is progressing.
    resetNoiseFloorBatch();
  }
}

unsigned long RadioLibWrapper::getMillis() const {
#if defined(ARDUINO)
  return millis();
#else
  return 0;
#endif
}

void RadioLibWrapper::setNoiseFloorCalibration(uint16_t sample_interval_ms, uint32_t max_calib_window_ms) {
  _noise_floor_sample_interval_ms = sample_interval_ms;
  _noise_floor_max_calib_window_ms = max_calib_window_ms;
  resetNoiseFloorBatch();
}

void RadioLibWrapper::doResetAGC() {
  _radio->sleep();  // warm sleep to reset analog frontend
}

void RadioLibWrapper::resetAGC() {
  // make sure we're not mid-receive of packet!
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) return;

  doResetAGC();
  state = STATE_IDLE;   // trigger a startReceive()

  // Reset the in-progress batch after the receiver frontend has been reset.
  // Keep the last published floor until a valid post-reset batch completes,
  // because early post-reset RSSI readings can sit at the low reporting bound.
  resetNoiseFloorBatch();
}

void RadioLibWrapper::loop() {
  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!isReceivingPacket()) {
      unsigned long now = getMillis();
      if (_noise_floor_batch_active &&
          _noise_floor_max_calib_window_ms > 0 &&
          elapsedAtLeast(now, _noise_floor_batch_started_at, _noise_floor_max_calib_window_ms)) {
        // A calibration window that cannot complete is probably observing
        // unstable receiver state or channel activity. Drop the partial
        // window so old accepted samples cannot mix with later conditions.
        resetNoiseFloorBatch();
        return;
      }
      if (_has_last_noise_floor_sample &&
          _noise_floor_sample_interval_ms > 0 &&
          !elapsedAtLeast(now, _last_noise_floor_sample_at, _noise_floor_sample_interval_ms)) {
        return;
      }
      if (!_noise_floor_batch_active) {
        _noise_floor_batch_started_at = now;
        _noise_floor_batch_active = true;
      }
      _last_noise_floor_sample_at = now;
      _has_last_noise_floor_sample = true;

      int16_t rssi = (int16_t)getCurrentRSSI();
      if (rssi < MIN_NOISE_FLOOR_SAMPLE) {
        rssi = MIN_NOISE_FLOOR_SAMPLE;
      }
      bool trusted_published_floor = isTrustedNoiseFloorValue(_noise_floor);
      bool no_trusted_floor = !trusted_published_floor;
      bool healthy_floor_would_jump_down = trusted_published_floor &&
          (_noise_floor - rssi) >= LOW_BOUND_REJECT_JUMP_DB;
      bool high_bound_sample = rssi >= MAX_NOISE_FLOOR_SAMPLE;
      bool healthy_floor_would_jump_up = trusted_published_floor &&
          (rssi - _noise_floor) >= HIGH_BOUND_REJECT_JUMP_DB;

      // Once a plausible floor exists, sudden large jumps are more likely to
      // be AGC/channel artefacts than a real idle-floor change. Without a
      // trusted floor, low RSSI samples are allowed because analyser readings
      // around -118 dBm, with occasional lower samples, are expected.
      if (healthy_floor_would_jump_down) {
        _floor_rejected_low_bound = mesh::incrementStatCounter(_floor_rejected_low_bound);
        return;
      }
      // Strong instantaneous RSSI is channel activity, not idle floor. Median
      // resists a few of these samples, but rejecting them keeps the batch
      // diagnostics honest and prevents a busy period from becoming the floor.
      if ((no_trusted_floor && high_bound_sample) || healthy_floor_would_jump_up) {
        _floor_rejected_high_bound = mesh::incrementStatCounter(_floor_rejected_high_bound);
        return;
      }

      if (_num_floor_samples == 0) {
        _floor_sample_min = rssi;
        _floor_sample_max = rssi;
      } else {
        if (rssi < _floor_sample_min) {
          _floor_sample_min = rssi;
        }
        if (rssi > _floor_sample_max) {
          _floor_sample_max = rssi;
        }
      }
      _floor_samples[_num_floor_samples] = rssi;
      _num_floor_samples++;

      if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {
        std::sort(_floor_samples, _floor_samples + NUM_NOISE_FLOOR_SAMPLES);
        _floor_sample_median = ((int32_t)_floor_samples[(NUM_NOISE_FLOOR_SAMPLES / 2) - 1] +
                                _floor_samples[NUM_NOISE_FLOOR_SAMPLES / 2]) / 2;
        int16_t floor_estimate = _floor_samples[NUM_NOISE_FLOOR_SAMPLES / 4];
        bool completed_high_activity_batch =
            no_trusted_floor && floor_estimate >= MAX_NOISE_FLOOR_SAMPLE;
        bool completed_batch_would_jump_down = trusted_published_floor &&
            (_noise_floor - floor_estimate) >= LOW_BOUND_REJECT_JUMP_DB;
        bool completed_batch_would_jump_up = trusted_published_floor &&
            (floor_estimate - _noise_floor) >= HIGH_BOUND_REJECT_JUMP_DB;

        // The per-sample gates should normally stop invalid batches before
        // they complete. Keep the publish path defensive as well, so a stale
        // or activity-filled batch cannot make the published floor jump.
        if (completed_batch_would_jump_down) {
          _floor_rejected_low_bound = addStatCounter(_floor_rejected_low_bound, _num_floor_samples);
          resetNoiseFloorSamples();
          return;
        }
        if (completed_high_activity_batch || completed_batch_would_jump_up) {
          _floor_rejected_high_bound = addStatCounter(_floor_rejected_high_bound, _num_floor_samples);
          resetNoiseFloorSamples();
          return;
        }

        _noise_floor = floor_estimate;

        MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor=%d accepted=%u min=%d median=%d max=%d rejected_low=%u rejected_high=%u",
          (int)_noise_floor,
          _num_floor_samples,
          (int)_floor_sample_min,
          (int)_floor_sample_median,
          (int)_floor_sample_max,
          _floor_rejected_low_bound,
          _floor_rejected_high_bound);
      }
    }
  }
}

void RadioLibWrapper::startRecv() {
  int err = _radio->startReceive();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

bool RadioLibWrapper::hasNoiseFloor() const {
  return isTrustedNoiseFloorValue(_noise_floor) && _num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES;
}

int RadioLibWrapper::getNoiseFloor() const {
  return isTrustedNoiseFloorValue(_noise_floor) ? _noise_floor : 0;
}

mesh::NoiseFloorStats RadioLibWrapper::getNoiseFloorStats() const {
  if (!isTrustedNoiseFloorValue(_noise_floor) && _num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {
    return {
      0,
      0,
      0,
      0,
      _floor_rejected_low_bound,
      _floor_rejected_high_bound
    };
  }

  return {
    _num_floor_samples,
    _floor_sample_min,
    _floor_sample_median,
    _floor_sample_max,
    _floor_rejected_low_bound,
    _floor_rejected_high_bound
  };
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  if (state & STATE_INT_READY) {
    len = _radio->getPacketLength();
    if (len > 0) {
      if (len > sz) { len = sz; }
      int err = _radio->readData(bytes, len);
      if (err != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
        len = 0;
        n_recv_errors++;
      } else {
        // Capture packet signal metrics while the completed packet status is
        // still the current radio context. Later instantaneous RSSI sampling
        // for carrier sense/noise floor must not change "last packet" stats.
        updateLastPacketMetrics(_radio->getRSSI(), _radio->getSNR());
      //  Serial.print("  readData() -> "); Serial.println(len);
        n_recv++;
      }
    }
    state = STATE_IDLE;   // need another startReceive()
  }

  if (state != STATE_RX) {
    int err = _radio->startReceive();
    if (err == RADIOLIB_ERR_NONE) {
      state = STATE_RX;
    } else {
      MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
    }
  }
  return len;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_TX_WAIT;
    return true;
  }
  MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit(%d)", err);
  idle();   // trigger another startRecv()
  _board->onAfterTransmit();
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  if (state & STATE_INT_READY) {
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  _radio->finishTransmit();
  _board->onAfterTransmit();
  state = STATE_IDLE;
}

int16_t RadioLibWrapper::performChannelScan() {
  return _radio->scanChannel();
}

bool RadioLibWrapper::isChannelActive() {
  // int.thresh: RSSI-based interference detection (relative to noise floor)
  if (_threshold != 0 && hasNoiseFloor()) {
    int16_t samples[RSSI_CARRIER_SENSE_SAMPLES];
    for (uint8_t i = 0; i < RSSI_CARRIER_SENSE_SAMPLES; i++) {
      samples[i] = (int16_t)getCurrentRSSI();
    }

    mesh::RssiCarrierSenseConfig config = {
      _noise_floor,
      _threshold,
      true,
      RSSI_CARRIER_SENSE_REQUIRED
    };

    // Instantaneous RSSI can spike briefly, so carrier sense requires a short
    // majority of busy samples. This is only a local RSSI guard; it cannot
    // detect hidden-node collisions at another receiver.
    if (mesh::RssiCarrierSense::isActive(config, samples, RSSI_CARRIER_SENSE_SAMPLES)) {
      return true;
    }
  }

  // cad: hardware channel activity detection
  if (_cad_enabled) {
    int16_t result = performChannelScan();
    // scanChannel() triggers DIO interrupt (CAD done) which sets STATE_INT_READY
    // via setFlag() ISR. Clear it before restarting RX so recvRaw() doesn't
    // try to read a non-existent packet and count a spurious recv error.
    state = STATE_IDLE;
    startRecv();
    if (result != RADIOLIB_CHANNEL_FREE) return true;
  }

  return false;
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
  if (sf < 7) return 0.0f;
  
  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0f;
  auto collision_penalty = 1.0f - (packet_len / 256.0f);   // Assuming max packet of 256 bytes

  return std::max(0.0f, std::min(1.0f, success_rate_based_on_snr * collision_penalty));
}
