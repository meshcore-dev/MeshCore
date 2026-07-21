#include "Dispatcher.h"

#if MESH_PACKET_LOGGING
  #include <Arduino.h>
#endif

#ifdef MESHCORE_SIMULATOR
  #include "sim_context.h"
#endif

#include <math.h>

namespace mesh {

#define MAX_RX_DELAY_MILLIS        32000  // 32 seconds
#define MIN_TX_BUDGET_RESERVE_MS   100    // min budget (ms) required before allowing next TX
#define MIN_TX_BUDGET_AIRTIME_DIV  2      // require at least 1/N of estimated airtime as budget before TX

#ifndef NOISE_FLOOR_CALIB_INTERVAL
  #define NOISE_FLOOR_CALIB_INTERVAL   2000     // 2 seconds
#endif

void Dispatcher::begin() {
  n_sent_flood = n_sent_direct = 0;
  n_recv_flood = n_recv_direct = 0;
  _err_flags = 0;
  radio_nonrx_start = _ms->getMillis();

  duty_cycle_window_ms = getDutyCycleWindowMs();
  float duty_cycle = 1.0f / (1.0f + getAirtimeBudgetFactor());
  tx_budget_ms = (unsigned long)(duty_cycle_window_ms * duty_cycle);
  last_budget_update = _ms->getMillis();

  _radio->begin();
  prev_isrecv_mode = _radio->isInRecvMode();
}

float Dispatcher::getAirtimeBudgetFactor() const {
  return 1.0;
}

void Dispatcher::updateTxBudget() {
  unsigned long now = _ms->getMillis();
  unsigned long elapsed = now - last_budget_update;

  float duty_cycle = 1.0f / (1.0f + getAirtimeBudgetFactor());
  unsigned long max_budget = (unsigned long)(getDutyCycleWindowMs() * duty_cycle);
  unsigned long refill = (unsigned long)(elapsed * duty_cycle);
  
  if (refill > 0) {
    tx_budget_ms += refill;
    if (tx_budget_ms > max_budget) {
      tx_budget_ms = max_budget;
    }
    last_budget_update = now;
  }
}

int Dispatcher::calcRxDelay(float score, uint32_t air_time) const {
  return (int) ((pow(10, 0.85f - score) - 1.0) * air_time);
}

uint32_t Dispatcher::getCADFailRetryDelay() const {
  return 200;
}
uint32_t Dispatcher::getCADFailMaxDuration() const {
  return 4000;   // 4 seconds
}

void Dispatcher::loop() {
  if (millisHasNowPassed(next_floor_calib_time)) {
    _radio->triggerNoiseFloorCalibrate(getInterferenceThreshold());
    _radio->setCADEnabled(getCADEnabled());
    next_floor_calib_time = futureMillis(NOISE_FLOOR_CALIB_INTERVAL);
  }
  _radio->loop();

  // check for radio 'stuck' in mode other than Rx
  bool is_recv = _radio->isInRecvMode();
  if (is_recv != prev_isrecv_mode) {
    prev_isrecv_mode = is_recv;
    if (!is_recv) {
      radio_nonrx_start = _ms->getMillis();
    }
  }
  if (!is_recv && _ms->getMillis() - radio_nonrx_start > 8000) {   // radio has not been in Rx mode for 8 seconds!
    _err_flags |= ERR_EVENT_STARTRX_TIMEOUT;
  }

  if (outbound) {  // waiting for outbound send to be completed
    if (_radio->isSendComplete()) {
      long t = _ms->getMillis() - outbound_start;
      total_air_time += t;
      //Serial.print("  airtime="); Serial.println(t);

      updateTxBudget();

      if (t > tx_budget_ms) {
        tx_budget_ms = 0;
      } else {
        tx_budget_ms -= t;
      }

      if (tx_budget_ms < MIN_TX_BUDGET_RESERVE_MS) {
        float duty_cycle = 1.0f / (1.0f + getAirtimeBudgetFactor());
        unsigned long needed = MIN_TX_BUDGET_RESERVE_MS - tx_budget_ms;
        next_tx_time = futureMillis((unsigned long)(needed / duty_cycle));
      } else {
        next_tx_time = _ms->getMillis();
      }

      _radio->onSendFinished();
      logTx(outbound, 2 + outbound->getPathByteLen() + outbound->payload_len);
      if (outbound->isRouteFlood()) {
        n_sent_flood++;
      } else {
        n_sent_direct++;
      }
      // allow for possible retransmission for reliability
      if (!resendPacket(outbound)) {
        releasePacket(outbound); // return to pool
      }
      outbound = NULL;
    } else if (millisHasNowPassed(outbound_expiry)) {
      MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): WARNING: outbound packed send timed out!", getLogDateTime());

      _radio->onSendFinished();
      logTxFail(outbound, 2 + outbound->getPathByteLen() + outbound->payload_len);

      releasePacket(outbound);  // return to pool
      outbound = NULL;
    } else {
      return;  // can't do any more radio activity until send is complete or timed out
    }

    // going back into receive mode now...
    next_agc_reset_time = futureMillis(getAGCResetInterval());
  }

  if (getAGCResetInterval() > 0 && millisHasNowPassed(next_agc_reset_time)) {
    _radio->resetAGC();
    next_agc_reset_time = futureMillis(getAGCResetInterval());
  }

  // check inbound (delayed) queue
  {
    Packet* pkt = _mgr->getNextInbound(_ms->getMillis());
    if (pkt) {
      processRecvPacket(pkt);
    }
  }
  checkRecv();
  checkSend();
}

bool Dispatcher::tryParsePacket(Packet* pkt, const uint8_t* raw, int len) {
  int i = 0;

  pkt->header = raw[i++];
  if (pkt->getPayloadVer() > PAYLOAD_VER_1) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): unsupported packet version", getLogDateTime());
    return false;
  }

  if (pkt->hasTransportCodes()) {
    memcpy(&pkt->transport_codes[0], &raw[i], 2); i += 2;
    memcpy(&pkt->transport_codes[1], &raw[i], 2); i += 2;
  } else {
    pkt->transport_codes[0] = pkt->transport_codes[1] = 0;
  }

  pkt->path_len = raw[i++];
  uint8_t path_mode = pkt->path_len >> 6;  // upper 2 bits (legacy firmware: 00)
  if (path_mode == 3) {   // Reserved for future
    MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): unsupported path mode: 3", getLogDateTime());
    return false;
  }

  uint8_t path_byte_len = (pkt->path_len & 63) * pkt->getPathHashSize();
  if (path_byte_len > MAX_PATH_SIZE || i + path_byte_len > len) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): partial or corrupt packet received, len=%d", getLogDateTime(), len);
    return false;
  }

  memcpy(pkt->path, &raw[i], path_byte_len); i += path_byte_len;

  pkt->payload_len = len - i;  // payload is remainder
  if (pkt->payload_len > sizeof(pkt->payload)) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): packet payload too big, payload_len=%d", getLogDateTime(), (uint32_t)pkt->payload_len);
    return false;
  }

  memcpy(pkt->payload, &raw[i], pkt->payload_len);

  return true;  // success
}

void Dispatcher::checkRecv() {
  Packet* pkt;
  float score;
  uint32_t air_time;
  {
    uint8_t raw[MAX_TRANS_UNIT+1];
    int len = _radio->recvRaw(raw, MAX_TRANS_UNIT);
    if (len > 0) {
      logRxRaw(_radio->getLastSNR(), _radio->getLastRSSI(), raw, len);

      pkt = _mgr->allocNew();
      if (pkt == NULL) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(): WARNING: received data, no unused packets available!", getLogDateTime());
      } else {
        if (tryParsePacket(pkt, raw, len)) {
          pkt->_snr = _radio->getLastSNR() * 4.0f;
          score = _radio->packetScore(_radio->getLastSNR(), len);
          air_time = _radio->getEstAirtimeFor(len);
          rx_air_time += air_time;
        } else {
          _mgr->free(pkt);  // put back into pool
          pkt = NULL;
        }
      }
    } else {
      pkt = NULL;
    }
  }
  if (pkt) {
    #if MESH_PACKET_LOGGING
    Serial.print(getLogDateTime());
    Serial.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d time=%d", 
            pkt->getRawLength(), pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
            (int)pkt->getSNR(), (int)_radio->getLastRSSI(), (int)(score*1000), air_time);

    pkt->calculatePacketHash();
    Serial.print(" hash=");
    mesh::Utils::printHex(Serial, pkt->hash, MAX_HASH_SIZE);

    if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ
        || pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
      Serial.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
    } else {
      Serial.printf("\n");
    }
    #endif
    logRx(pkt, pkt->getRawLength(), score);   // hook for custom logging

    if (pkt->isRouteFlood()) {
      n_recv_flood++;

      int _delay = calcRxDelay(score, air_time);
      if (_delay < 50) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(), score delay below threshold (%d)", getLogDateTime(), _delay);
        processRecvPacket(pkt);   // is below the score delay threshold, so process immediately
      } else {
        MESH_DEBUG_PRINTLN("%s Dispatcher::checkRecv(), score delay is: %d millis", getLogDateTime(), _delay);
        if (_delay > MAX_RX_DELAY_MILLIS) {
          _delay = MAX_RX_DELAY_MILLIS;
        }
        _mgr->queueInbound(pkt, futureMillis(_delay)); // add to delayed inbound queue
      }
    } else {
      n_recv_direct++;
      processRecvPacket(pkt);
    }
  }
}

void Dispatcher::processRecvPacket(Packet* pkt) {
  DispatcherAction action = onRecvPacket(pkt);
  if (action == ACTION_RELEASE) {
    _mgr->free(pkt);
  } else if (action == ACTION_MANUAL_HOLD) {
    // sub-class is wanting to manually hold Packet instance, and call releasePacket() at appropriate time
  } else {   // ACTION_RETRANSMIT*
    uint8_t priority = (action >> 24) - 1;
    uint32_t _delay = action & 0xFFFFFF;

    _mgr->queueOutbound(pkt, priority, futureMillis(_delay));
  }
}

void Dispatcher::checkSend() {
  if (_mgr->getOutboundCount(_ms->getMillis()) == 0) return;
  
  updateTxBudget();
  
  uint32_t est_airtime = _radio->getEstAirtimeFor(MAX_TRANS_UNIT);
  if (tx_budget_ms < est_airtime / MIN_TX_BUDGET_AIRTIME_DIV) {
    float duty_cycle = 1.0f / (1.0f + getAirtimeBudgetFactor());
    unsigned long needed = est_airtime / MIN_TX_BUDGET_AIRTIME_DIV - tx_budget_ms;
    next_tx_time = futureMillis((unsigned long)(needed / duty_cycle));
    return;
  }
  
  if (!millisHasNowPassed(next_tx_time)) return;

  // Pick the channel-busy check by packet kind. Resends (direct, already attempted)
  // use a NON-invasive gate via isResendChannelActive() so the radio stays in RX and
  // can still overhear the downstream forward → resend cancellation. First sends keep
  // the CAD-based carrier sense (collision avoidance worth the momentary deafness).
  Packet* pending = _mgr->peekNextOutbound(_ms->getMillis());
  bool resend_lbt = pending && pending->isRouteDirect() && pending->sending_attempts > 0;
  bool channel_busy = resend_lbt ? isResendChannelActive() : _radio->isReceiving();

  if (channel_busy) {
    if (cad_busy_start == 0) {
      cad_busy_start = _ms->getMillis();   // record when CAD busy state started
    }

    if (_ms->getMillis() - cad_busy_start > getCADFailMaxDuration()) {
      _err_flags |= ERR_EVENT_CAD_TIMEOUT;

      MESH_DEBUG_PRINTLN("%s Dispatcher::checkSend(): CAD busy max duration reached!", getLogDateTime());
      // channel activity has gone on too long... (Radio might be in a bad state)
      // force the pending transmit below...
    } else {
      if (resend_lbt) {
        // Deterministic per-hop busy-recheck bucket for DIRECT resends (Problem 1: when an
        // interferer clears, deferred resenders that share a re-check cadence fire on the same
        // loop tick and collide). getPathHashCount() decreases by exactly 1 each forwarding hop,
        // so the originator and every relay of the SAME direct packet carry distinct counts
        // (N, N-1, N-2, ...). count%3 therefore assigns mutually-in-range chain neighbours to
        // different re-check cadences: any two nodes <=2 hops apart differ by <=2 in count, hence
        // are always distinct mod 3. The only same-bucket pairs are >=3 hops apart and do not
        // radio-interfere. Same {120,240,360} ms range as the random draw -> no latency inflation,
        // and NOT a quiet-dwell-style window-inflating stagger (the cancel window in
        // resendPacket() is untouched). First sends keep the randomized getCADFailRetryDelay().
        uint32_t pos = pending->getPathHashCount() % 3;
        next_tx_time = futureMillis((pos + 1) * 120);
      } else {
        next_tx_time = futureMillis(getCADFailRetryDelay());
      }
      return;
    }
  }
  cad_busy_start = 0;  // reset busy state

  outbound = _mgr->getNextOutbound(_ms->getMillis());
  if (outbound) {
    int len = 0;
    uint8_t raw[MAX_TRANS_UNIT];

    raw[len++] = outbound->header;
    if (outbound->hasTransportCodes()) {
      memcpy(&raw[len], &outbound->transport_codes[0], 2); len += 2;
      memcpy(&raw[len], &outbound->transport_codes[1], 2); len += 2;
    }
    raw[len++] = outbound->path_len;
    len += Packet::writePath(&raw[len], outbound->path, outbound->path_len);

    if (len + outbound->payload_len > MAX_TRANS_UNIT) {
      MESH_DEBUG_PRINTLN("%s Dispatcher::checkSend(): FATAL: Invalid packet queued... too long, len=%d", getLogDateTime(), len + outbound->payload_len);
      _mgr->free(outbound);
      outbound = NULL;
    } else {
      memcpy(&raw[len], outbound->payload, outbound->payload_len); len += outbound->payload_len;

      uint32_t max_airtime = _radio->getEstAirtimeFor(len)*3/2;
      outbound_start = _ms->getMillis();
      bool success = _radio->startSendRaw(raw, len);
      if (!success) {
        MESH_DEBUG_PRINTLN("%s Dispatcher::loop(): ERROR: send start failed!", getLogDateTime());

        logTxFail(outbound, outbound->getRawLength());
  
        releasePacket(outbound);  // return to pool
        outbound = NULL;
        return;
      }
      outbound_expiry = futureMillis(max_airtime);

    #if MESH_PACKET_LOGGING
      Serial.print(getLogDateTime());
      Serial.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d, attempt=%d)", len,
                    outbound->getPayloadType(), outbound->isRouteDirect() ? "D" : "F", outbound->payload_len, outbound->sending_attempts);
      if (outbound->getPayloadType() == PAYLOAD_TYPE_PATH || outbound->getPayloadType() == PAYLOAD_TYPE_REQ
        || outbound->getPayloadType() == PAYLOAD_TYPE_RESPONSE || outbound->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        Serial.printf(" [%02X -> %02X]\n", (uint32_t)outbound->payload[1], (uint32_t)outbound->payload[0]);
      } else {
        Serial.printf("\n");
      }
    #endif
    }
  }
}

Packet* Dispatcher::obtainNewPacket() {
  auto pkt = _mgr->allocNew();  // TODO: zero out all fields
  if (pkt == NULL) {
    _err_flags |= ERR_EVENT_FULL;
  } else {
    pkt->payload_len = pkt->path_len = 0;
    pkt->_snr = 0;
  }
  return pkt;
}

void Dispatcher::releasePacket(Packet* packet) {
  _mgr->free(packet);
}

void Dispatcher::sendPacket(Packet* packet, uint8_t priority, uint32_t delay_millis) {
  if (!Packet::isValidPathLen(packet->path_len) || packet->payload_len > MAX_PACKET_PAYLOAD) {
    MESH_DEBUG_PRINTLN("%s Dispatcher::sendPacket(): ERROR: invalid packet... path_len=%d, payload_len=%d", getLogDateTime(), (uint32_t) packet->path_len, (uint32_t) packet->payload_len);
    _mgr->free(packet);
  } else {
    _mgr->queueOutbound(packet, priority, futureMillis(delay_millis));
  }
}

// Utility function -- handles the case where millis() wraps around back to zero
//   2's complement arithmetic will handle any unsigned subtraction up to HALF the word size (32-bits in this case)
bool Dispatcher::millisHasNowPassed(unsigned long timestamp) const {
  return (long)(_ms->getMillis() - timestamp) > 0;
}

unsigned long Dispatcher::futureMillis(int millis_from_now) const {
  unsigned long wake_time = _ms->getMillis() + millis_from_now;
#ifdef MESHCORE_SIMULATOR // Register wake time with simulator for accurate scheduling
  if (auto *ctx = SIM_CTX()) {
    ctx->wake_registry.registerWakeTime(wake_time);
  }
#endif
  return wake_time;
}

bool Dispatcher::resendPacket(mesh::Packet *packet) {

  // Pool-shedding: under low free-pool pressure, do NOT queue a resend. A resend consumes a
  // pool slot shared with RX (allocNew in checkRecv); under sustained load it would exhaust
  // the pool and deafen this node. The PRIMARY direct forward already transmitted, so shedding
  // here only drops the REDUNDANT retry.
  if (_mgr->getFreeCount() <= POOL_SHED_FREE_THRESHOLD) {
    return false;
  }

  // prepare error correction via potential retransmit:
  // re-send only direct routed packets that carry at least one relay hash, so that a
  // downstream relay's forward can be overheard to cancel this re-send.
  // The final relay hop (path empty after removeSelfFromPath, flagged via
  // final_hop_ack_resend) is the exception: there is no downstream forward to overhear, but
  // the destination ACKs receipt. Allow exactly one resend there (sending_attempts == 0),
  // cancellable by the returning ACK (see Mesh::cancelPendingFinalHopResend).
  if (packet->isRouteDirect() && packet->sending_attempts < getMaxResendAttempts() &&
      (packet->getPathHashCount() > 0 ||
       (packet->final_hop_ack_resend && packet->sending_attempts == 0))) {
    packet->sending_attempts++;

    MESH_DEBUG_PRINTLN("Dispatcher::resendPacket %s attempt=%d", packet->getHashHex(),
                       packet->sending_attempts);

    // Resend window: W = C0 + K*airtime + margin + (attempt-1)*jitter. C0 (RESEND_DEFERRAL_FIXED_MS)
    // is a static, SF-independent constant calibrated to the measured fixed forward-latency
    // component; the K*airtime term carries the SF-dependence (airtime comes from getEstAirtimeFor(),
    // i.e. from the configured SF/BW/CR), so the window tracks the spreading factor automatically —
    // no per-regime constant. The K*airtime term already covers the downstream relay's forward TX
    // and its getDirectRetransmitDelay() spread, so the originator's OWN direct-retransmit delay is
    // NOT added on top here — it would only inflate the wait without covering any forward-latency
    // component. getDirectRetransmitDelay() stays in force for actual relaying (see Mesh.cpp).
    uint32_t packet_airtime_ms = _radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2);
    uint32_t cancel_window = resendWindowFor((uint16_t)packet_airtime_ms, packet->sending_attempts);
    _mgr->queueOutbound(packet, 1, futureMillis((int)cancel_window));
    return true;
  }

  return false;
}

// W = C0 + K*airtime + margin + (attempt-1)*jitter, capped at an AIRTIME-PROPORTIONAL ceiling
// (CAP_BASE + CAP_AIRTIME_X*airtime). C0 = RESEND_DEFERRAL_FIXED_MS (static); the K*airtime term
// carries SF-dependence. The ceiling grows with airtime (CAP_AIRTIME_X >= K) so it never clips the
// formula's K*airtime term for long packets / high SF — the earlier flat 1500 ms ceiling under-covered
// SF10 long messages (downstream forward alone is ~2-3 s), so the resend fired before the forward
// could cancel it and every long message picked up a redundant, colliding resend.
uint32_t Dispatcher::resendWindowFor(uint16_t airtime_ms, uint8_t sending_attempts) const {
  uint32_t w = (uint32_t)RESEND_DEFERRAL_FIXED_MS
             + (uint32_t)airtime_ms * RESEND_DEFERRAL_AIRTIME_X
             + RESEND_DEFERRAL_MARGIN_MS
             + (uint32_t)(sending_attempts - 1) * RESEND_BACKOFF_JITTER_MS;
  uint32_t cap = (uint32_t)RESEND_DEFERRAL_CAP_BASE_MS
               + (uint32_t)airtime_ms * RESEND_DEFERRAL_CAP_AIRTIME_X;
  if (w > cap) w = cap;
  return w;
}

}