#pragma once

#include <MeshCore.h>
#include <Identity.h>
#include <Packet.h>
#include <Utils.h>
#include <RadioProfiles.h>
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
  virtual RadioProfiles* profiles() { return nullptr; }
  virtual const RadioProfiles* profiles() const { return nullptr; }
  virtual bool validateProfile(const RadioProfileParams&) const { return false; }
  virtual uint8_t receiveProfile() const { return 0; }
  virtual uint32_t receiveProfileGeneration() const {
    const auto* p = profiles();
    return p ? p->generation[receiveProfile()] : 0;
  }
  virtual RadioParamApplyResult prepareTransmitProfile(uint8_t profile) {
    return profile == 0 ? RadioParamApplyResult::APPLIED : RadioParamApplyResult::FAILED;
  }
  virtual RadioParamApplyResult trySetPrimaryParams(const RadioProfileParams&, bool = false,
                                                   const uint32_t* = nullptr) {
    return RadioParamApplyResult::FAILED;
  }
  virtual uint16_t profilePreamble(uint8_t) const { return 0; }
  virtual uint32_t getProfileAirtime(uint8_t, int len_bytes, uint8_t = 0) {
    return getEstAirtimeFor(len_bytes);
  }

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

  // On rejection the caller retains ownership of the packet.
  virtual bool queueOutbound(Packet* packet, uint8_t priority, uint32_t scheduled_for) = 0;
  virtual Packet* peekNextOutbound(uint32_t now) const = 0;
  virtual bool deferOutbound(Packet* packet, uint32_t scheduled_for) = 0;
  virtual Packet* getNextOutbound(uint32_t now) = 0;    // by priority
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

/**
 * \brief  The low-level task that manages detecting incoming Packets, and the queueing
 *      and scheduling of outbound Packets.
*/
class Dispatcher {
  Packet* outbound;  // current outbound packet
  unsigned long outbound_expiry, outbound_start, total_air_time, rx_air_time;
  unsigned long next_tx_time;
  unsigned long cad_busy_start;
  unsigned long radio_nonrx_start;
  unsigned long next_floor_calib_time, next_agc_reset_time;
  bool  prev_isrecv_mode;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  unsigned long tx_budget_ms;
  unsigned long last_budget_update;
  unsigned long duty_cycle_window_ms;
  uint8_t receive_context_profile = 0;
  uint32_t receive_context_generation = 0;
  bool receive_context_active = false;
  uint32_t profile_cad_busy_start[2] = {};
  uint32_t profile_cad_generation[2] = {};
  bool profile_cad_busy_active[2] = {};
  uint32_t next_transmit_token = 0;

  void processRecvPacket(Packet* pkt);
  void updateTxBudget();

protected:
  class ReceiveProfileScope {
    Dispatcher& owner;
    uint8_t old_profile;
    uint32_t old_generation;
    bool old_active;
  public:
    ReceiveProfileScope(Dispatcher& dispatcher, uint8_t profile, uint32_t generation)
        : owner(dispatcher), old_profile(owner.receive_context_profile),
          old_generation(owner.receive_context_generation), old_active(owner.receive_context_active) {
      owner.receive_context_profile = profile;
      owner.receive_context_generation = generation;
      owner.receive_context_active = true;
    }
    ~ReceiveProfileScope() {
      owner.receive_context_profile = old_profile;
      owner.receive_context_generation = old_generation;
      owner.receive_context_active = old_active;
    }
    ReceiveProfileScope(const ReceiveProfileScope&) = delete;
    ReceiveProfileScope& operator=(const ReceiveProfileScope&) = delete;
  };
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
  // One result per physical copy. A crossed response can fail on one profile
  // and succeed on the other; callers commit only after an observed success.
  virtual void onTransmitResult(uint32_t token, bool success) { }
  virtual const char* getLogDateTime() { return ""; }

  virtual float getAirtimeBudgetFactor() const;
  virtual int calcRxDelay(float score, uint32_t air_time) const;
  virtual uint32_t getCADFailRetryDelay() const;
  virtual uint32_t getCADFailMaxDuration() const;
  virtual int getInterferenceThreshold() const { return 0; }    // disabled by default
  virtual bool getCADEnabled() const { return false; }    // hardware CAD disabled by default
  virtual int getAGCResetInterval() const { return 0; }    // disabled by default
  virtual unsigned long getDutyCycleWindowMs() const { return 3600000; }

public:
  void begin();
  void loop();
  Radio* getProfileRadio() { return _radio; }
  bool isDualRadioActive() const { return _radio->profiles() && _radio->profiles()->enabled(); }
  bool hasOutbound() const { return outbound != nullptr; }
  bool hasPendingTransmissions() const { return outbound != nullptr || _mgr->getOutboundTotal() != 0; }
  uint32_t reserveTransmitToken() {
    if (++next_transmit_token == 0) ++next_transmit_token;
    return next_transmit_token;
  }
  void setTransmitToken(Packet* packet, uint32_t token) {
    if (packet) packet->transmit_token = token;
  }

  Packet* obtainNewPacket();
  void releasePacket(Packet* packet);
  void sendPacket(Packet* packet, uint8_t priority, uint32_t delay_millis=0);

  unsigned long getTotalAirTime() const { return total_air_time; }
  unsigned long getReceiveAirTime() const {return rx_air_time; }
  unsigned long getRemainingTxBudget() const { return tx_budget_ms; }
  uint32_t getNumSentFlood() const { return n_sent_flood; }
  uint32_t getNumSentDirect() const { return n_sent_direct; }
  uint32_t getNumRecvFlood() const { return n_recv_flood; }
  uint32_t getNumRecvDirect() const { return n_recv_direct; }
  void resetStats() {
    n_sent_flood = n_sent_direct = n_recv_flood = n_recv_direct = 0;
    _err_flags = 0;
  }

  // helper methods
  bool millisHasNowPassed(unsigned long timestamp) const;
  unsigned long futureMillis(int millis_from_now) const;

  bool tryParsePacket(Packet* pkt, const uint8_t* raw, int len);

private:
  bool isPacketRadioCurrent(const Packet* packet) const;
  void checkRecv();
  void checkSend();
};

}
