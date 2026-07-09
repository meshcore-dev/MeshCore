
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

static volatile uint8_t state = STATE_IDLE;

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
}

uint32_t RadioLibWrapper::getRngSeed() {
  return _radio->random(0x7FFFFFFF);
}

void RadioLibWrapper::setTxPower(int8_t dbm) {
  _cur_dbm = dbm;
  _dbm_valid = true;
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
  // make sure we're not mid-receiving and mid-sending of packet!
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket() || (state == STATE_TX_WAIT)) return;

  doResetAGC();
  state = STATE_IDLE;   // trigger a startReceive()

  // Reset noise floor sampling so it reconverges from scratch.
  // Without this, a stuck _noise_floor of -120 makes the sampling threshold
  // too low (-106) to accept normal samples (~-105), self-reinforcing the
  // stuck value even after the receiver has recovered.
  _noise_floor = 0;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

void RadioLibWrapper::rxPsWatchdogCheck() {
  // don't interfere mid-transmit or with a completed-but-unread packet
  if ((state & STATE_INT_READY) != 0 || (state & ~STATE_INT_READY) == STATE_TX_WAIT) return;

  unsigned long now = millis();
  bool tripped = false;

  if (_rx_ps_armed && state == STATE_RX && _wd_stuck_thresh > 0) {
    bool busy = isChipBusy();
    if (busy != _wd_last_busy) {
      _wd_last_busy = busy;
      _wd_last_transition = now;
      _wd_stage = 0;    // the sleep/listen wave is present -> radio healthy
    } else if (now - _wd_last_transition > _wd_stuck_thresh) {
      tripped = true;   // BUSY frozen: chip fell out of the duty cycle with no IRQ
    }
  }
  if (_startrx_fails >= 3) tripped = true;  // can't even re-arm receive mode

  if (!tripped) return;

  _wd_last_transition = now;   // grace period before the next escalation
  _startrx_fails = 0;

  if (_wd_stage == 0) {
    _wd_stage = 1;
    n_wd_soft++;
    MESH_DEBUG_PRINTLN("RadioLibWrapper: watchdog: RX duty-cycle stuck, soft re-arm");
    state = STATE_IDLE;   // next recvRaw() re-arms receive mode
  } else {
    _wd_stage = 2;
    n_wd_hard++;
    MESH_DEBUG_PRINTLN("RadioLibWrapper: watchdog: still stuck, hard radio reset");
    if (radioDeepInit()) {
      _rx_ps_armed = false;   // chip is factory-fresh after NRST
      _radio->setPacketReceivedAction(setFlag);
      if (_params_valid) setParams(_cur_freq, _cur_bw, _cur_sf, _cur_cr);
      if (_dbm_valid) _radio->setOutputPower(_cur_dbm);
    }
    state = STATE_IDLE;   // re-arm (rx powersaving settings are kept in members)
  }
}

void RadioLibWrapper::loop() {
  if (_rx_ps_enabled) {
    rxPsWatchdogCheck();
  }

  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    // In RX duty-cycle (powersaving) mode only sample while the chip is in a
    // listen window: during the sleep window the frontend is off, so the RSSI
    // is meaningless and the SPI read would stall until the next window.
    if (!(_rx_ps_armed && isChipBusy()) && !isReceivingPacket()) {
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

    MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);
  }
}

void RadioLibWrapper::startRecv() {
  int err = startReceiveMode();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
    _startrx_fails = 0;
    if (_rx_ps_armed) {
      // (re)base the duty-cycle watchdog on the freshly armed cycle
      _wd_last_busy = isChipBusy();
      _wd_last_transition = millis();
      // Longest legitimate silence on the BUSY pin: one full cycle, plus the
      // extended RX after a (possibly false) preamble detect (2*rx + sleep),
      // plus a worst-case packet airtime, plus margin for TCXO/transitions.
      uint32_t rx_ms = _rx_ps_rx_us / 1000, sleep_ms = _rx_ps_sleep_us / 1000;
      _wd_stuck_thresh = (rx_ms + sleep_ms) + 2 * (2 * rx_ms + sleep_ms)
                         + getEstAirtimeFor(MAX_TRANS_UNIT) + 1000;
    }
  } else {
    if (_startrx_fails < 255) _startrx_fails++;
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceiveMode(%d)", err);
  }
}

int RadioLibWrapper::startReceiveMode() {
  return _radio->startReceive();
}

void RadioLibWrapper::stopReceiveDutyCycle() {
  // The duty-cycle sequencer only stops on RxDone or an explicit standby;
  // issuing other mode commands while it runs leads to undefined behaviour.
  _radio->standby();
  _rx_ps_armed = false;
}

bool RadioLibWrapper::isPacketReady() {
  if (!_rx_ps_armed) return true;   // continuous RX: DIO1 only fires for RxDone/TxDone here

  // In duty-cycle RX the DIO1 interrupt also fires for RX timeout (false
  // preamble detect) and header errors. GetRxBufferStatus still reports the
  // *previous* packet's length then, so reading the buffer would re-deliver
  // stale bytes as a ghost packet. Only read when the radio reports RxDone.
  // (checkIrq errors are treated as ready, falling back to old behaviour.)
  return _radio->checkIrq(RADIOLIB_IRQ_RX_DONE) != 0;
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

bool RadioLibWrapper::setRxPowerSaving(bool enabled, uint32_t rx_us, uint32_t sleep_us) {
  if (enabled && !supportsRxPowerSaving()) {
    return false;
  }

  _rx_ps_enabled = enabled;
  _rx_ps_rx_us = rx_us;
  _rx_ps_sleep_us = sleep_us;
  // Force the next recvRaw() to arm the requested RX mode, but don't clobber a
  // completed-but-unread packet (STATE_INT_READY): recvRaw() will consume it and
  // then re-arm with the new mode. Also leave an in-flight TX alone. (Same
  // non-atomic guard style as resetAGC().)
  if ((state & STATE_INT_READY) == 0 && (state & ~STATE_INT_READY) != STATE_TX_WAIT) {
    state = STATE_IDLE;
  }
  return true;
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  if (state & STATE_INT_READY) {
    if (isPacketReady()) {
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
    }
    state = STATE_IDLE;   // need another startReceive()
  }

  if (state != STATE_RX) {
    startRecv();
  }
  return len;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  if (_rx_ps_armed) {
    // stop the duty-cycle sequencer before SetTx, otherwise its next RTC
    // event can fire mid-transmission and abort the TX
    stopReceiveDutyCycle();
  }
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
  // int.thresh: RSSI-based interference detection (relative to noise floor).
  // In RX duty-cycle mode only checked while the chip is in a listen window
  // (during the sleep window the frontend is off and the read would stall).
  if (_threshold != 0 && !(_rx_ps_armed && isChipBusy())
      && getCurrentRSSI() > _noise_floor + _threshold) return true;

  // cad: hardware channel activity detection
  if (_cad_enabled) {
    int16_t result = performChannelScan();
    // scanChannel() triggers DIO interrupt (CAD done) which sets STATE_INT_READY
    // via setFlag() ISR. Clear it before restarting RX so recvRaw() doesn't
    // try to read a non-existent packet and count a spurious recv error.
    state = STATE_IDLE;
    startRecv();
    if (result != RADIOLIB_CHANNEL_FREE) {
      _board->n_cad_busy++;
      return true;
    }
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
