
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

static volatile uint8_t state = STATE_IDLE;

// In-place insertion sort of int16_t samples for the noise-floor median. Runs once per
// calibration block (64 elements, ~every 2 s of idle), so O(n^2) is irrelevant here.
static void sortInt16(int16_t* a, int n) {
  for (int i = 1; i < n; i++) {
    int16_t key = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > key) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = key;
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
  _floor_block_ready = false;
  _last_floor_sample_at = 0;
  _held_block_count = 0;

  _last_rx_sync_check = millis();
  _rx_desync_streak = 0;
  n_rx_desync_events = n_rx_desync_fatals = 0;
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
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {  // restart only once the current block is complete
    _num_floor_samples = 0;
    _floor_block_ready = false;
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

  // Discard any in-progress noise-floor block: the analog frontend was just reset, so
  // queued RSSI samples are stale. _noise_floor itself is left in place — the median
  // estimator no longer drifts to -120 (the reason the old ratchet needed a hard
  // _noise_floor = 0 reset), and forcing 0 here would create a brief permissive LBT
  // window (margin = RSSI - 0) until the next block completes.
  _num_floor_samples = 0;
  _floor_block_ready = false;
  _held_block_count = 0;   // contamination context is stale after an AFE reset
}

void RadioLibWrapper::loop() {
  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    uint32_t now = millis();
    if (!isReceivingPacket() && now - _last_floor_sample_at >= NOISE_FLOOR_SAMPLE_INTERVAL_MS) {
      // Accept every idle sample, spaced NOISE_FLOOR_SAMPLE_INTERVAL_MS apart so the block spans a real
      // ~3.2 s window and the median rejects transient transmissions (not a few-ms snapshot). The old
      // "rssi < floor + threshold" filter was a one-way ratchet: it only accepted samples below the
      // current floor, so the block average drifted to the -120 clamp and never recovered — leaving
      // _noise_floor stuck low and the RSSI-margin LBT permanently over-sensitive.
      _floor_samples[_num_floor_samples++] = (int16_t)getCurrentRSSI();
      _last_floor_sample_at = now;
    }
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && !_floor_block_ready) {
    // Block complete: reduce to the median. The median rejects transient interference
    // spikes (high and low outliers) and recovers in BOTH directions, unlike the ratcheted
    // mean. _noise_floor is written only here, so the previous value stays valid while the
    // next block is sampled — no reset-to-0, no permissive LBT window during reconvergence.
    sortInt16(_floor_samples, NUM_NOISE_FLOOR_SAMPLES);
    int16_t median = (int16_t)(((int32_t)_floor_samples[NUM_NOISE_FLOOR_SAMPLES / 2 - 1]
                              + (int32_t)_floor_samples[NUM_NOISE_FLOOR_SAMPLES / 2]) / 2);
    // One-sided hold: a median jumping far ABOVE the published floor is activity-contaminated
    // (inter-packet energy slips past the !isReceivingPacket() idle guard). Hold the old value so
    // the RSSI-margin LBT stays meaningful under load; near-stable/quieter blocks publish at once.
    // First block always publishes (_noise_floor=0 from begin()), so the hold binds only post-boot.
    //
    // Bounded: after NOISE_FLOOR_MAX_HELD_BLOCKS consecutive held blocks accept the median, else a real
    // permanent rise is held forever (stuck-floor bug from the other direction). Count-based so the hold
    // rides out load bursts (slow blocks) while a quiet rise releases in a few blocks.
    if (median > _noise_floor + NOISE_FLOOR_MAX_RISE_DB) {
      _held_block_count++;
      if (_held_block_count >= NOISE_FLOOR_MAX_HELD_BLOCKS) {
        _noise_floor = median;
        if (_noise_floor < -120) {
          _noise_floor = -120;    // clamp to lower bound of -120dBi
        }
        _held_block_count = 0;
        #ifdef MESH_DEBUG_NOISE_FLOOR
        MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d (accepted after %d held blocks, persistent rise)",
                           (int)_noise_floor, NOISE_FLOOR_MAX_HELD_BLOCKS);
        #endif
      } else {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor held at %d (block median %d contaminated, held %d/%d)",
                           (int)_noise_floor, (int)median, _held_block_count, NOISE_FLOOR_MAX_HELD_BLOCKS);
      }
    } else {
      _held_block_count = 0;
      _noise_floor = median;
      if (_noise_floor < -120) {
        _noise_floor = -120;    // clamp to lower bound of -120dBi
      }
      #ifdef MESH_DEBUG_NOISE_FLOOR
      MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d (median)", (int)_noise_floor);
      #endif
    }
    _floor_block_ready = true;
  }

  // --- RX-desync watchdog ---
  // `state` is firmware-side truth. If the chip silently leaves RX (supply dip during
  // TX, SPI glitch, front-end upset), the RAM copy still says STATE_RX, so recvRaw()
  // never re-arms and the Dispatcher-side 8 s check — reading the same variable —
  // stays quiet: the node goes deaf until reboot. (Visible symptom: the Current-RSSI
  // register freezes at the last energy seen, pinning the noise floor high.)
  // Here we ask the CHIP instead: verifyRxChipMode() reads its real operating mode.
  // Radio types without an authoritative status register report true (watchdog off).
  // Recovery: after RX_DESYNC_CONFIRM_TICKS bad polls, re-arm the receiver; if that
  // doesn't take, escalate to a warm sleep (resets the modem state machine/AFE).
  // A streak that survives both is counted fatal and surfaces as ERR_EVENT_RX_DESYNC.
  if (state == STATE_RX) {
    uint32_t now = millis();
    if (now - _last_rx_sync_check >= RX_DESYNC_CHECK_INTERVAL_MS) {
      _last_rx_sync_check = now;
      if (!isReceivingPacket() && !verifyRxChipMode()) {
        if (_rx_desync_streak == 0) {
          n_rx_desync_events++;
          MESH_DEBUG_PRINTLN("RadioLibWrapper: RX desync - chip reports mode != RX");
        }
        _rx_desync_streak++;
        // samples taken while wedged read a frozen RSSI register: discard the block
        _num_floor_samples = 0;
        _floor_block_ready = false;
        _held_block_count = 0;
        if (_rx_desync_streak == RX_DESYNC_CONFIRM_TICKS) {
          MESH_DEBUG_PRINTLN("RadioLibWrapper: RX desync confirmed - re-arming receiver");
          idle();          // standby, then a fresh startReceive()
          startRecv();
        } else if (_rx_desync_streak > RX_DESYNC_CONFIRM_TICKS) {
          if (_rx_desync_streak == RX_DESYNC_FATAL_STREAK) {
            n_rx_desync_fatals++;
            MESH_DEBUG_PRINTLN("RadioLibWrapper: RX desync survived AFE reset - radio damaged, reboot suggested");
          }
          // warm sleep resets the analog frontend and modem state machine;
          // resetAGC() re-arms from STATE_IDLE via recvRaw() on the next pass
          resetAGC();
        }
      } else if (_rx_desync_streak > 0) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: RX desync resolved after %u ticks", _rx_desync_streak);
        _rx_desync_streak = 0;
      }
    }
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
      //  Serial.print("  readData() -> "); Serial.println(len);
        n_recv++;
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

float RadioLibWrapper::getLastRSSI() const {
  return _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  return _radio->getSNR();
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
