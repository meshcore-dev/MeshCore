
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#include <Arduino.h>   // millis()

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

// Channel is considered busy when the live RSSI sits this far above the noise
// floor. Fixed (not the configurable _threshold, which companions disable):
// must be >= SAMPLING_THRESHOLD or the energy the floor calibrator tolerates
// (floor+14) would already trip the busy verdict on plain noise.
#define CHAN_BUSY_MARGIN 15

// Rate limit for the busy-verdict RSSI poll (one SPI transaction each).
#define CHAN_BUSY_RSSI_INTERVAL_MS 50

// RX-quality jam verdict: ambient-jam share of the last ~5 s that counts as
// "currently jammed"...
#define RXQ_JAM_MIN_PCT  80
// ...combined with no decodable attempt for this long. A jammed channel produces
// zero header-valid IRQs, i.e. ZERO RX-quality events, so the pure event ratio
// would freeze at its last (healthy) value for the whole 10 min window - the row
// would read 100% exactly while nothing can be received.
#define RXQ_JAM_STALE_MS  120000

static volatile uint8_t state = STATE_IDLE;

// In-place insertion sort of int16_t samples for the quiet-floor percentile. Runs
// once per calibration block (64 elements), so O(n^2) is irrelevant here.
static void sortInt16(int16_t* a, int n) {
  for (int i = 1; i < n; i++) {
    int16_t v = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
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

  // start average out some samples
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
  _quiet_floor_cnt = 0;
  _quiet_floor_idx = 0;
  _quiet_floor = 0;   // busyRefFloor() falls back to _noise_floor (capped) until the ring fills
  _cur_jam = false;
}

uint32_t RadioLibWrapper::getRngSeed() {
  return _radio->random(0x7FFFFFFF);
}

void RadioLibWrapper::setTxPower(int8_t dbm) {
#if defined(USE_LR2021)
  idle();
#endif
  _radio->setOutputPower(dbm);
}

void RadioLibWrapper::idle() {
  _radio->standby();
  state = STATE_IDLE;   // need another startReceive()
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {  // ignore trigger if currently sampling
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void RadioLibWrapper::doResetAGC() {
  _radio->sleep();  // warm sleep to reset analog frontend
}

void RadioLibWrapper::resetAGC() {
  // make sure we're not mid-receive of packet!
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) return;

  doResetAGC();
  state = STATE_IDLE;   // trigger a startReceive()

  // Reset noise floor sampling so it reconverges from scratch.
  // Without this, a stuck _noise_floor of -120 makes the sampling threshold
  // too low (-106) to accept normal samples (~-105), self-reinforcing the
  // stuck value even after the receiver has recovered.
  _noise_floor = 0;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;

  // channel-health metrics: stamp now so the first window has no phantom sample
  _last_metric_ms = _last_rssi_ms = millis();
  _last_recv_cnt = n_recv;
  _last_strong_err_cnt = n_recv_errors_strong;
  _cur_busy = false;
  _cur_jam = false;   // (the quiet-floor ring stays: published history is not invalidated by an AFE reset)
}

int16_t RadioLibWrapper::busyRefFloor() {
  int16_t ref = (_quiet_floor_cnt >= QUIET_FLOOR_MIN_BLOCKS) ? _quiet_floor : _noise_floor;
  if (ref > CHAN_BUSY_REF_MAX_DB) ref = CHAN_BUSY_REF_MAX_DB;
  return ref;
}

bool RadioLibWrapper::getRxQualityPct(uint8_t& pct) {
  uint16_t ev, bad;
  _err_win.counts(ev, bad);
  if (ev == 0) { pct = 0; return false; }  // nothing observed yet: no verdict
  // Sustained interference with no decode attempt at all is a reception failure,
  // not "no traffic": report 0% instead of a ratio frozen at its last healthy value.
  if (_jam_win.pct() >= RXQ_JAM_MIN_PCT && millis() - _last_rxq_ev_ms >= RXQ_JAM_STALE_MS) {
    pct = 0;
    return true;
  }
  pct = (uint8_t)(((ev - bad) * 100u) / ev);
  return true;
}

void RadioLibWrapper::loop() {
  // --- windowed channel-health metrics (time-weighted, loop-rate independent) ---
  // Busy covers what the radio cannot afford to miss: our own TX airtime (the
  // receiver cannot measure while transmitting), an in-progress reception, or
  // energy above floor + margin. The RX-based verdicts are sampled on the
  // CHAN_BUSY_RSSI_INTERVAL_MS tick, NOT on every loop() call (the main loop
  // spins at kHz on ESP32): 2 SPI transactions per tick instead of thousands
  // per second. This is a pure observation margin and deliberately independent
  // of the send gate's operator-configured verdict in isChannelActive()
  // (int.thresh / CAD): the display should not change just because the
  // operator retunes when the node is allowed to send.
  // Deaf-but-not-TX windows (FIFO readout, TX turnaround; each us..few ms)
  // count as not-busy but stay in the denominator: a small, deliberate
  // underestimate of utilization. (CAD dwells are attributed by
  // isChannelActive() itself, where they block.)
  uint32_t now = millis();
  uint32_t dt = now - _last_metric_ms; _last_metric_ms = now;
  bool in_rx = isInRecvMode();
  bool tx = ((state & ~STATE_INT_READY) == STATE_TX_WAIT);
  if (tx) {
    _cur_busy = true;
    _cur_jam = false;   // our own transmission is not ambient interference
  } else if (in_rx && now - _last_rssi_ms >= CHAN_BUSY_RSSI_INTERVAL_MS) {
    _last_rssi_ms = now;
    // Never call isReceivingPacket() while a completed packet is unread
    // (STATE_INT_READY): on SX126x its header-error branch clears HEADER_ERR,
    // which readData() needs to classify the packet - clearing it beforehand
    // would count a header-damaged packet as a good decode. The RSSI poll
    // still marks the channel busy while that packet drains.
    bool mid_rx = ((state & STATE_INT_READY) == 0) && isReceivingPacket();
    int16_t rssi = (int16_t)getCurrentRSSI();
    int16_t ref = busyRefFloor();
    _cur_busy = mid_rx || (rssi > ref + CHAN_BUSY_MARGIN);
    // Ambient jam: the same energy test, but strictly against the QUIET reference and
    // never while locked onto a preamble (that is a packet, not interference). This is
    // the Dauerstoerer detector: the adapted _noise_floor follows a sustained
    // interferer up to its level, so busy measured against _noise_floor would call the
    // channel "free" exactly while nothing can be decoded.
    _cur_jam = !mid_rx && (rssi > ref + CHAN_BUSY_MARGIN);
  } else if (!in_rx) {
    _cur_busy = false;   // out of RX without TX: nothing measurable, never hold a stale verdict
    _cur_jam = false;
  }
  _busy_win.add(now, _cur_busy ? dt : 0);
  _deaf_win.add(now, in_rx ? 0 : dt);
  _jam_win.add(now, _cur_jam ? dt : 0);
  uint32_t r = n_recv, es = n_recv_errors_strong;   // counter deltas -> RX-quality window
  uint16_t d_ok = (uint16_t)(r - _last_recv_cnt), d_err = (uint16_t)(es - _last_strong_err_cnt);
  // events = decodes + SNR-relevant CRC failures (weak distant stations are
  // excluded in recvRaw, from both numerator and denominator), bad = those failures
  _err_win.add(now, d_ok + d_err, d_err);
  if (d_ok + d_err > 0) _last_rxq_ev_ms = now;
  _last_recv_cnt = r; _last_strong_err_cnt = es;

  // --- noise floor sampling ---
  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!isReceivingPacket()) {
      int rssi = getCurrentRSSI();
      if (rssi < _noise_floor + SAMPLING_THRESHOLD) {  // only consider samples below current floor + sampling THRESHOLD
        _num_floor_samples++;
        _floor_sample_sum += rssi;
      }
    }
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && _floor_sample_sum != 0) {
    _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (_noise_floor < -120) {
      _noise_floor = -120;    // clamp to lower bound of -120dBi
    }
    _floor_sample_sum = 0;

    // Quiet-floor ring: retain the published values and keep their 10th percentile as
    // the busy-verdict reference. The adapted floor can drift (ratchet) or follow a
    // sustained interferer (other estimator lineages); the quietest decile of the last
    // several minutes stays near the real ambient, and CHAN_BUSY_REF_MAX_DB bounds even
    // a jam that outlives the ring. Recovers on its own once quiet blocks return.
    _quiet_floor_ring[_quiet_floor_idx] = _noise_floor;
    _quiet_floor_idx = (_quiet_floor_idx + 1) % QUIET_FLOOR_BLOCKS;
    if (_quiet_floor_cnt < QUIET_FLOOR_BLOCKS) _quiet_floor_cnt++;
    {
      int16_t sorted[QUIET_FLOOR_BLOCKS];
      for (uint8_t i = 0; i < _quiet_floor_cnt; i++) sorted[i] = _quiet_floor_ring[i];
      sortInt16(sorted, _quiet_floor_cnt);
      _quiet_floor = sorted[_quiet_floor_cnt / 10];
      #ifdef MESH_DEBUG_NOISE_FLOOR
      MESH_DEBUG_PRINTLN("RadioLibWrapper: quiet_floor = %d (P10 of %u blocks)", (int)_quiet_floor, _quiet_floor_cnt);
      #endif
    }

    #ifdef MESH_DEBUG_NOISE_FLOOR
    MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);
    #endif
  }
}

void RadioLibWrapper::startRecv() {
  #if defined(USE_LR2021)
  _radio->standby(); // without this LR2021 can throw -706 when calling startReceive after hardware CAD when side detectors are enabled
  #endif
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

// Approximate SNR threshold per SF for successful reception (based on Semtech datasheets)
static float snr_threshold[] = {
    -7.5,  // SF7 needs at least -7.5 dB SNR
    -10,   // SF8 needs at least -10 dB SNR
    -12.5, // SF9 needs at least -12.5 dB SNR
    -15,  // SF10 needs at least -15 dB SNR
    -17.5,// SF11 needs at least -17.5 dB SNR
    -20   // SF12 needs at least -20 dB SNR
};

// A CRC-failed packet counts as an RX-quality failure only if its SNR was this
// far above the per-SF decode threshold: "should have decoded, but didn't" =
// collision/interference verdict on this channel. Distant stations below the
// decode threshold are physics, not channel health.
#define RXQ_FAIL_SNR_GUARD_DB 3.0f

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
        // Only "relevant" failures enter the RX-quality window: a packet whose
        // SNR says it SHOULD have decoded (>= per-SF threshold + guard) but
        // failed CRC indicates a collision/interference on THIS channel, while
        // a distant station below the decode threshold is expected to fail.
        // The packet-status SNR stays latched after the failed read (readData
        // clears IRQ/FIFO state, not packet status) - but it is only
        // trustworthy once ANY packet has latched a status: before that it
        // reads the 0 dB reset value, which passes every threshold + guard.
        // Header-damaged receptions may still read the previous packet's
        // latch (the modem aborted before the payload): best effort.
        // Weak failures drop out of both numerator and denominator.
        if (_rx_snr_latched && (err == RADIOLIB_ERR_CRC_MISMATCH || err == RADIOLIB_ERR_LORA_HEADER_DAMAGED)) {
          uint8_t sf = getSpreadingFactor();
          if (sf < 7) sf = 7; else if (sf > 12) sf = 12;
          float snr = getLastSNR();
          bool relevant = (snr >= snr_threshold[sf - 7] + RXQ_FAIL_SNR_GUARD_DB);
          if (relevant) n_recv_errors_strong++;
          #ifdef MESH_DEBUG_RXQ
          MESH_DEBUG_PRINTLN("RXQ fail: snr=%.1f sf=%u -> %s", (double)snr, sf, relevant ? "counted" : "excluded(weak)");
          #endif
        }
      } else {
      //  Serial.print("  readData() -> "); Serial.println(len);
        n_recv++;
        _rx_snr_latched = true;  // a packet status is now latched -> SNR verdicts are meaningful
      }
    }
    #if defined(USE_LR2021)
    state = STATE_RX;     // LR2021 stays in Rx after readData, calling startReceive while still in Rx throws -706 errors
    #else
    state = STATE_IDLE;   // need another startReceive()
    #endif
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
  if (_threshold != 0 && getCurrentRSSI() > _noise_floor + _threshold) return true;

  // cad: hardware channel activity detection
  if (_cad_enabled) {
    // The CAD runs in standby (radio NOT listening) and blocks this thread for
    // ms: attribute the dwell to the deafness window here, where it happens -
    // once loop() next runs the radio is back in RX and the dwell would
    // otherwise vanish from both metrics.
    uint32_t cad_start = millis();
    int16_t result = performChannelScan();
    uint32_t cad_end = millis();
    _deaf_win.add(cad_end, cad_end - cad_start);
    // scanChannel() triggers DIO interrupt (CAD done) which sets STATE_INT_READY
    // via setFlag() ISR. Clear it before restarting RX so recvRaw() doesn't
    // try to read a non-existent packet and count a spurious recv error.
    state = STATE_IDLE;
    startRecv();
    if (result != RADIOLIB_CHANNEL_FREE) return true;
  }

  return false;
}

float RadioLibWrapper::getLastRSSI() const {
  return _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  return _radio->getSNR();
}

float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;

  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}

PacketMillis RadioLibWrapper::calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint8_t preambleSymbols) {
  // based on RadioLib's calculateTimeOnAir()
  uint32_t tsym_us = ((uint32_t)10000 << sf) / (bw * 10);
  uint32_t sfCoeff1_x4 = (sf == 5 || sf == 6) ? 25 : 17; // 6.25 : 4.25, semtech magic numbers to account for sync word + sfd

  // preamble + syncword + sfd + header
  uint32_t preamble_us = (((preambleSymbols + 8) * 4 + sfCoeff1_x4) * tsym_us) / 4;

  // airtime for max packet at current radio settings
  uint32_t total_us   = _radio->getTimeOnAir(MAX_TRANS_UNIT);
  // airtime for payload only (no preamble, header or SOF)
  uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us : 4000 - preamble_us; // fallback to 4 secs at worst case
  // rescale payload_us for max possible CR
  if (cr >= 5 && cr < 8) { payload_us = (payload_us * 8) / cr; }

  return PacketMillis {(preamble_us + 999) / 1000, (payload_us + 999) / 1000};
}
