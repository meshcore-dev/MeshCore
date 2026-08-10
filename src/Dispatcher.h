#pragma once

#include <MeshCore.h>
#include <Identity.h>
#include <Packet.h>
#include <Utils.h>
#include <string.h>

namespace mesh {

/**
 * \brief  Abstraction of local/volatile clock with Millisecond granularity.
*/
class MillisecondClock {
public:
  virtual unsigned long getMillis() = 0;
};

/**
 * \brief  Abstraction of this device's packet radio.
*/
class Radio {
public:
  virtual void begin() { }

  /**
   * \brief  polls for incoming raw packet.
   * \param  bytes  destination to store incoming raw packet.
   * \param  sz   maximum packet size allowed.
   * \returns 0 if no incoming data, otherwise length of complete packet received.
  */
  virtual int recvRaw(uint8_t* bytes, int sz) = 0;

  /**
   * \returns  estimated transmit air-time needed for packet of 'len_bytes', in milliseconds.
  */
  virtual uint32_t getEstAirtimeFor(int len_bytes) = 0;

  virtual float packetScore(float snr, int packet_len) = 0;

  /**
   * \brief  starts the raw packet send. (no wait)
   * \param  bytes   the raw packet data
   * \param  len  the length in bytes
   * \returns true if successfully started
  */
  virtual bool startSendRaw(const uint8_t* bytes, int len) = 0;

  /**
   * \returns true if the previous 'startSendRaw()' completed successfully.
  */
  virtual bool isSendComplete() = 0;

  /**
   * \brief  a hook for doing any necessary clean up after transmit.
  */
  virtual void onSendFinished() = 0;

  /**
   * \brief  do any processing needed on each loop cycle
   */
  virtual void loop() { }

  virtual int getNoiseFloor() const { return 0; }

  virtual void triggerNoiseFloorCalibrate(int threshold) { }

  virtual void setCADEnabled(bool enable) { }

  virtual void resetAGC() { }

  virtual bool isInRecvMode() const = 0;

  /**
   * \returns  true if the radio is currently mid-receive of a packet.
  */
  virtual bool isReceiving() { return false; }

  virtual float getLastRSSI() const { return 0; }
  virtual float getLastSNR() const { return 0; }
};

/**
 * \brief  An abstraction for managing instances of Packets (eg. in a static pool),
 *        and for managing the outbound packet queue.
*/
class PacketManager {
public:
  virtual Packet* allocNew() = 0;
  virtual void free(Packet* packet) = 0;

  virtual void queueOutbound(Packet* packet, uint8_t priority, uint32_t scheduled_for) = 0;
  virtual Packet* getNextOutbound(uint32_t now) = 0;    // by priority
  virtual Packet* peekNextOutbound(uint32_t now) { return NULL; }   // same as getNextOutbound but non-consuming (default: none)
  virtual int getOutboundCount(uint32_t now) const = 0;
  virtual int getOutboundTotal() const = 0;
  virtual int getFreeCount() const = 0;
  virtual Packet* getOutboundByIdx(int i) = 0;
  virtual Packet* removeOutboundByIdx(int i) = 0;
  virtual void queueInbound(Packet* packet, uint32_t scheduled_for) = 0;
  virtual Packet* getNextInbound(uint32_t now) = 0;
};

typedef uint32_t  DispatcherAction;

#define ACTION_RELEASE           (0)
#define ACTION_MANUAL_HOLD       (1)
#define ACTION_RETRANSMIT(pri)   (((uint32_t)1 + (pri))<<24)
#define ACTION_RETRANSMIT_DELAYED(pri, _delay)  ((((uint32_t)1 + (pri))<<24) | (_delay))

#define ERR_EVENT_FULL              (1 << 0)
#define ERR_EVENT_CAD_TIMEOUT       (1 << 1)
#define ERR_EVENT_STARTRX_TIMEOUT   (1 << 2)

// Pool-shedding backpressure: when the free packet pool (slots available for allocNew/RX) drops
// to or below this threshold, skip DIRECT resends (resendPacket) to protect RX liveness. The
// PRIMARY direct forward is never shed (different path) — only redundant resends. Tune per pool
// size (repeaters=32, companions=16); default covers a short RX burst.
#ifndef POOL_SHED_FREE_THRESHOLD
  #define POOL_SHED_FREE_THRESHOLD  6
#endif

// Resend window model. The resend must wait for the DOWNSTREAM relay's forward to arrive and
// cancel it via isRetryMatch. The forward latency decomposes into:
//   - a fixed part: relay processing + LBT/CAD-retry (in 120ms units) + optional dwell + scheduling,
//   - an airtime-proportional part: the relay's forward TX (~1x airtime) + its retransmit_delay.
// The window is therefore modelled as:   W = C0 + K*airtime + margin + (attempt-1)*jitter
// C0 (RESEND_DEFERRAL_FIXED_MS) is a STATIC value calibrated from observer runs — the measured
// fixed forward-latency component, SF-INDEPENDENT because the SF-dependent forward TX airtime is
// carried entirely by the K*airtime term (getEstAirtimeFor already encodes the configured SF/BW/CR).
// So one formula covers any spreading factor without a per-regime constant or an SF branch. The
// former flat 5x-airtime multiplier could not represent the fixed part and under-covered it at low SF
// (SF5/SF8), where airtime is only a few ms while the fixed relay deferral is ~600 ms. (An earlier
// runtime-adaptive C via EWMA on overheard forwards was removed as over-engineering: the forward
// latency is stable, so the EWMA converged to the same value a correctly-set static C0 gives.)
#ifndef RESEND_DEFERRAL_FIXED_MS
  // C0: fixed (airtime-INDEPENDENT) forward-latency component = relay processing + CAD-retry +
  // quiet-dwell gate + scheduling. Recalibrated for SF10 + quiet-dwell from the long-message HW A/B
  // (feature/repeated-sending-3, 2026-07-21): prompt-forward latency ~2.85 s minus the 2*airtime term
  // (~1.56 s for a 160 B / ~780 ms-airtime packet) leaves a fixed residual of ~1.29 s median, with the
  // prompt-forward cluster reaching ~1.76 s. The prior 600 ms was calibrated on SF8 observer runs and
  // under-covered SF10 long messages, where the downstream forward alone takes 2-3 s — so the resend
  // became eligible before the forward could cancel it and every long message picked up a redundant,
  // colliding resend. 1500 ms covers the prompt-forward regime; a heavily congested repeater (forward
  // pushed to 5-10 s) will still race, but that is a channel-load problem, not a window-sizing one.
  #define RESEND_DEFERRAL_FIXED_MS   1500
#endif
#ifndef RESEND_DEFERRAL_AIRTIME_X
  #define RESEND_DEFERRAL_AIRTIME_X  2     // K: forward TX (1x) + mean downstream retransmit_delay (~1x), expressed in airtimes
#endif
#ifndef RESEND_DEFERRAL_MARGIN_MS
  #define RESEND_DEFERRAL_MARGIN_MS  40    // extra safety for scheduling jitter / latency variance
#endif
// Airtime-PROPORTIONAL ceiling on W, replacing the earlier flat RESEND_DEFERRAL_MAX_MS (=1500). The
// flat cap clipped the formula's K*airtime term for any payload > ~85 B at SF10, so long messages were
// pinned at 1500 ms regardless of length — below even the fastest observed forward latency. The
// ceiling now grows with airtime: CAP_BASE + CAP_AIRTIME_X*airtime. CAP_AIRTIME_X (3) >= K (2), so the
// ceiling always sits above the formula for realistic packets and the formula governs; it only bounds
// genuinely pathological cases (very high SF / extreme airtime) to keep recovery latency finite.
#ifndef RESEND_DEFERRAL_CAP_BASE_MS
  #define RESEND_DEFERRAL_CAP_BASE_MS   1500  // ceiling floor (tiny-packet / low-SF case)
#endif
#ifndef RESEND_DEFERRAL_CAP_AIRTIME_X
  #define RESEND_DEFERRAL_CAP_AIRTIME_X 3     // ceiling slope in airtimes (>= K so it never under-covers)
#endif
#ifndef RESEND_BACKOFF_JITTER_MS
  #define RESEND_BACKOFF_JITTER_MS   100   // per-attempt stretch (attempt-1)*100 - the light stretch for 2nd/3rd attempt
#endif

/**
 * \brief  The low-level task that manages detecting incoming Packets, and the queueing
 *      and scheduling of outbound Packets.
*/
class Dispatcher {
protected:
  Packet* outbound;  // current outbound packet

private:
  unsigned long outbound_expiry, outbound_start, total_air_time, rx_air_time;
  unsigned long next_tx_time;
  unsigned long cad_busy_start;
  unsigned long radio_nonrx_start;
  unsigned long next_floor_calib_time, next_agc_reset_time;
  bool  prev_isrecv_mode;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint32_t n_resent_direct;  // subset of n_sent_direct that were resend TXs (sending_attempts>0)
  unsigned long tx_budget_ms;
  unsigned long last_budget_update;
  unsigned long duty_cycle_window_ms;

  // W = C0 + K*airtime + margin + (attempt-1)*jitter, with C0 = RESEND_DEFERRAL_FIXED_MS (static).
  uint32_t resendWindowFor(uint16_t airtime_ms, uint8_t sending_attempts) const;

  void processRecvPacket(Packet* pkt);
  void updateTxBudget();

protected:
  PacketManager* _mgr;
  Radio* _radio;
  MillisecondClock* _ms;
  uint16_t _err_flags;

  Dispatcher(Radio& radio, MillisecondClock& ms, PacketManager& mgr)
    : _radio(&radio), _ms(&ms), _mgr(&mgr)
  {
    outbound = NULL;
    total_air_time = rx_air_time = 0;
    next_tx_time = ms.getMillis();
    cad_busy_start = 0;
    next_floor_calib_time = next_agc_reset_time = 0;
    _err_flags = 0;
    radio_nonrx_start = 0;
    prev_isrecv_mode = true;
    tx_budget_ms = 0;
    last_budget_update = 0;
    duty_cycle_window_ms = 3600000;
  }

  virtual DispatcherAction onRecvPacket(Packet* pkt) = 0;

  virtual void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) { }   // custom hook

  virtual void logRx(Packet* packet, int len, float score) { }   // hooks for custom logging
  virtual void logTx(Packet* packet, int len) { }
  virtual void logTxFail(Packet* packet, int len) { }
  virtual const char* getLogDateTime() { return ""; }

  virtual float getAirtimeBudgetFactor() const;
  virtual int calcRxDelay(float score, uint32_t air_time) const;
  virtual uint32_t getCADFailRetryDelay() const;
  virtual uint32_t getCADFailMaxDuration() const;
  virtual int getInterferenceThreshold() const { return 0; }    // disabled by default
  virtual bool getCADEnabled() const { return false; }    // hardware CAD disabled by default
  // Channel-busy check used ONLY for resend TX gating (direct packets with sending_attempts>0).
  // Default falls back to CAD carrier sense; examples override with a NON-invasive check
  // (preamble/header IRQ + RSSI margin) so RX stays open to overhear the downstream forward
  // and cancel the resend. Non-const because _radio->isReceiving() is non-const.
  virtual bool isResendChannelActive() { return _radio->isReceiving(); }
  virtual int getAGCResetInterval() const { return 0; }    // disabled by default
  virtual unsigned long getDutyCycleWindowMs() const { return 3600000; }

  /**
   * \returns  maximum number of direct-route resend attempts (0 = disabled, default = 2, max = 3).
   */
  virtual uint8_t getMaxResendAttempts() const { return 2; }

public:
  void begin();
  void loop();

  Packet* obtainNewPacket();
  void releasePacket(Packet* packet);
  void sendPacket(Packet* packet, uint8_t priority, uint32_t delay_millis=0);
  /**
   * \brief  re-send the given packet (for retransmission) if conditions apply.
   * \return true, if packet was re-sent.
   */
  bool resendPacket(Packet *packet);

  /**
   * \returns  number of milliseconds delay to apply to retransmitting the given packet.
   */
  virtual uint32_t getRetransmitDelay(const Packet *packet) { return 0; };

  /**
   * \returns  number of milliseconds delay to apply to retransmitting the given packet, for DIRECT mode.
   */
  virtual uint32_t getDirectRetransmitDelay(const Packet *packet) { return 0; };

  unsigned long getTotalAirTime() const { return total_air_time; }
  unsigned long getReceiveAirTime() const {return rx_air_time; }
  unsigned long getRemainingTxBudget() const { return tx_budget_ms; }
  uint32_t getNumSentFlood() const { return n_sent_flood; }
  uint32_t getNumSentDirect() const { return n_sent_direct; }
  uint32_t getNumRecvFlood() const { return n_recv_flood; }
  uint32_t getNumRecvDirect() const { return n_recv_direct; }
  // resend TXs (subset of n_sent_direct): direct-route transmissions with sending_attempts>0, i.e.
  // re-sends rather than first/forward sends. Reported as n_resends / n_sent_direct. NOTE: TRACE
  // last-hop forwards pre-set sending_attempts (Mesh.cpp) and therefore also tally here, by design.
  uint32_t getNumResentDirect() const { return n_resent_direct; }
  void resetStats() {
    n_sent_flood = n_sent_direct = n_recv_flood = n_recv_direct = n_resent_direct = 0;
    _err_flags = 0;
  }

  // helper methods
  bool millisHasNowPassed(unsigned long timestamp) const;
  unsigned long futureMillis(int millis_from_now) const;

  bool tryParsePacket(Packet* pkt, const uint8_t* raw, int len);

private:
  void checkRecv();
  void checkSend();
};

}
