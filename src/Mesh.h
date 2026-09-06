#pragma once

#include <Dispatcher.h>

namespace mesh {

class GroupChannel {
public:
  uint8_t hash[PATH_HASH_SIZE];
  uint8_t secret[PUB_KEY_SIZE];
};

/**
 * An abstraction of the data tables needed to be maintained
*/
class MeshTables {
public:
  virtual bool wasSeen(const Packet* packet) = 0;
  virtual void markSeen(const Packet* packet) = 0;
  virtual void clear(const Packet* packet) = 0;    // remove this packet hash from table
};

#ifndef MAX_PENDING_NEXTHOP_CONFIRMS
  #define MAX_PENDING_NEXTHOP_CONFIRMS  4   // max concurrent direct packets awaiting next-hop confirmation
#endif

#define NEXTHOP_CONFIRM_REPEAT  0   // confirmed by overhearing the next hop repeat this same packet
#define NEXTHOP_CONFIRM_REPLY   1   // confirmed by seeing a correlated RESPONSE routed back through us

/**
 * \brief  Tracks a direct (path-routed) packet this node has repeated (or originated), while it
 *     waits for implicit confirmation of receipt -- either by overhearing the next hop repeat it
 *     (NEXTHOP_CONFIRM_REPEAT), or, when this is the last hop before the destination and there's
 *     no next hop to overhear, by seeing the correlated RESPONSE it provokes routed back through
 *     us (NEXTHOP_CONFIRM_REPLY).
*/
struct PendingNextHopConfirm {
  bool active;
  uint8_t kind;           // NEXTHOP_CONFIRM_REPEAT or NEXTHOP_CONFIRM_REPLY
  uint8_t retries;
  uint32_t deadline;      // millis() at which to retry (or give up if retries exhausted)
  uint8_t hash[MAX_HASH_SIZE];   // kind == NEXTHOP_CONFIRM_REPEAT: hash of the packet we're waiting to hear repeated
  uint8_t expect_dest_hash;      // kind == NEXTHOP_CONFIRM_REPLY: dest_hash expected on the correlated RESPONSE
  Packet pkt;             // copy of the packet as it was (re)transmitted, for resending
};

/**
 * \brief  The next layer in the basic Dispatcher task, Mesh recognises the particular Payload TYPES,
 *     and provides virtual methods for sub-classes on handling incoming, and also preparing outbound Packets.
*/
class Mesh : public Dispatcher {
  RTCClock* _rtc;
  RNG* _rng;
  MeshTables* _tables;
  PendingNextHopConfirm _pending_confirms[MAX_PENDING_NEXTHOP_CONFIRMS];

  void removeSelfFromPath(Packet* packet);
  void routeDirectRecvAcks(Packet* packet, uint32_t delay_millis);
  //void routeRecvAcks(Packet* packet, uint32_t delay_millis);
  DispatcherAction forwardMultipartDirect(Packet* pkt);

  /**
   * \brief  Start tracking 'pkt' (just repeated by this node) until the next hop is heard repeating it.
   */
  void registerNextHopConfirm(const Packet* pkt);

  /**
   * \brief  Start tracking 'pkt' (a REQ just delivered to its final destination, with no further
   *     hop to overhear) until a correlated RESPONSE is seen routed back through this node.
   */
  void registerLastHopReplyConfirm(const Packet* pkt);

  /**
   * \brief  Check an incoming direct packet against the pending-confirm table, and mark any
   *     match as confirmed -- either a next hop repeating a tracked packet, or a RESPONSE
   *     correlated (by dest_hash) to a tracked last-hop REQ delivery.
   */
  void checkNextHopConfirm(const Packet* pkt);

  /**
   * \brief  Called each loop(), resends any pending packets whose confirm deadline has passed,
   *     up to getNextHopMaxRetries() times, then drops them.
   */
  void processNextHopRetries();

protected:
  DispatcherAction onRecvPacket(Packet* pkt) override;

  virtual uint32_t getCADFailRetryDelay() const override;

  /**
   * \brief  Decide what to do with received packet, ie. discard, forward, or hold
   */
  DispatcherAction routeRecvPacket(Packet* packet);

  /**
   * \brief    Called _before_ the packet is dispatched to the on..Recv() methods.
   * \returns  true, if given packet should NOT be processed.
   */
  virtual bool filterRecvFloodPacket(Packet* packet) { return false; }

  /**
   * \brief  Check whether this packet should be forwarded (re-transmitted) or not.
   *     Is sub-classes responsibility to make sure given packet is only transmitted ONCE (by this node)
   */
  virtual bool allowPacketForward(const Packet* packet);

  /**
   * \returns  number of milliseconds delay to apply to retransmitting the given packet.
   */
  virtual uint32_t getRetransmitDelay(const Packet* packet);

  /**
   * \returns  number of milliseconds delay to apply to retransmitting the given packet, for DIRECT mode.
   */
  virtual uint32_t getDirectRetransmitDelay(const Packet* packet);

  /**
   * \returns  number of extra (Direct) ACK transmissions wanted.
   */
  virtual uint8_t getExtraAckTransmitCount() const;

  /**
   * \returns  true if 'next-hop reliability' (listen-for-repeat retry) is enabled for repeated
   *     direct packets. Default is enabled wherever allowPacketForward() also permits forwarding.
   */
  virtual bool getNextHopReliabilityEnabled() const { return true; }

  /**
   * \returns  max number of retries (resends) attempted, if no repeat from the next hop is heard.
   */
  virtual uint8_t getNextHopMaxRetries() const { return 3; }

  /**
   * \returns  number of milliseconds to wait for the next hop to repeat 'packet', before retrying.
   */
  virtual uint32_t getNextHopConfirmTimeout(const Packet* packet) const;

  /**
   * \brief  Perform search of local DB of peers/contacts.
   * \returns  Number of peers with matching hash
   */
  virtual int searchPeersByHash(const uint8_t* hash);

  /**
   * \brief  lookup the ECDH shared-secret between this node and peer by idx (calculate if necessary)
   * \param  dest_secret  destination array to copy the secret (must be PUB_KEY_SIZE bytes)
   * \param  peer_idx  index of peer, [0..n) where n is what searchPeersByHash() returned
   */
  virtual void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) { }

  /**
   * \brief  A (now decrypted) data packet has been received (by a known peer).
   *         NOTE: these can be received multiple times (per sender/msg-id), via different routes
   * \param  type  one of: PAYLOAD_TYPE_TXT_MSG, PAYLOAD_TYPE_REQ, PAYLOAD_TYPE_RESPONSE
   * \param  sender_idx  index of peer, [0..n) where n is what searchPeersByHash() returned
   * \param  secret   the pre-calculated shared-secret (handy for sending response packet)
   * \param  data   decrypted data from payload
  */
  virtual void onPeerDataRecv(Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) { }

  /**
   * \brief  A TRACE packet has been received. (and has reached the end of its given path)
   *         NOTE: this may have been initiated by another node.
   * \param  tag         a random (unique-ish) tag set by initiator
   * \param  auth_code   a code to authenticate the packet
   * \param  flags       zero for now
   * \param  path_snrs   single byte SNR*4 for each hop in the path
   * \param  path_hashes hashes of each repeater in the path
   * \param  path_len    length of the path_snrs[] and path_hashes[] arrays
  */
  virtual void onTraceRecv(Packet* packet, uint32_t tag, uint32_t auth_code, uint8_t flags, const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) { }

  /**
   * \brief  A path TO peer (sender_idx) has been received. (also with optional 'extra' data encoded)
   *         NOTE: these can be received multiple times (per sender), via different routes
   * \param  sender_idx  index of peer, [0..n) where n is what searchPeersByHash() returned
   * \param  secret   the pre-calculated shared-secret (handy for sending response packet)
   * \returns   true, if path was accepted and that reciprocal path should be sent
  */
  virtual bool onPeerPathRecv(Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) { return false; }

  /**
   * \brief  A new incoming Advertisement has been received.
   *         NOTE: these can be received multiple times (per id/timestamp), via different routes
  */
  virtual void onAdvertRecv(Packet* packet, const Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) { }

  /**
   * \brief  A (now decrypted) data packet has been received.
   *         NOTE: these can be received multiple times (per sender/contents), via different routes
   * \param  secret  ECDH shared secret
   * \param  sender  public key provided by sender
  */
  virtual void onAnonDataRecv(Packet* packet, const uint8_t* secret, const Identity& sender, uint8_t* data, size_t len) { }

  /**
   * \brief  A path TO 'sender' has been received. (also with optional 'extra' data encoded)
   *         NOTE: these can be received multiple times (per sender), via different routes
  */
  virtual void onPathRecv(Packet* packet, Identity& sender, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) { }

  /**
   * \brief  A control packet has been received.
  */
  virtual void onControlDataRecv(Packet* packet) { }

  /**
   * \brief  A packet with PAYLOAD_TYPE_RAW_CUSTOM has been received.
  */
  virtual void onRawDataRecv(Packet* packet) { }

  /**
   * \brief  Perform search of local DB of matching GroupChannels.
   * \param  channels  OUT - store matching channels in this array, up to max_matches
   * \returns  Number of channels with matching hash
   */
  virtual int searchChannelsByHash(const uint8_t* hash, GroupChannel channels[], int max_matches);

  /**
   * \brief  An encrypted group data packet has been received.
   *         NOTE: the same payload can be received multiple times, via different routes
   * \param  type  one of: PAYLOAD_TYPE_GRP_TXT, PAYLOAD_TYPE_GRP_DATA
   * \param  channel  the matching GroupChannel
  */
  virtual void onGroupDataRecv(Packet* packet, uint8_t type, const GroupChannel& channel, uint8_t* data, size_t len) { }

  /**
   * \brief  A simple ACK packet has been received.
   *         NOTE: same ACK can be received multiple times, via different routes
  */
  virtual void onAckRecv(Packet* packet, uint32_t ack_crc) { }

  Mesh(Radio& radio, MillisecondClock& ms, RNG& rng, RTCClock& rtc, PacketManager& mgr, MeshTables& tables)
    : Dispatcher(radio, ms, mgr), _rng(&rng), _rtc(&rtc), _tables(&tables)
  {
    memset(_pending_confirms, 0, sizeof(_pending_confirms));
  }

  MeshTables* getTables() const { return _tables; }

public:
  void begin();
  void loop();

  LocalIdentity self_id;

  RNG* getRNG() const { return _rng; }
  RTCClock* getRTCClock() const { return _rtc; }

  Packet* createAdvert(const LocalIdentity& id, const uint8_t* app_data=NULL, size_t app_data_len=0);
  Packet* createDatagram(uint8_t type, const Identity& dest, const uint8_t* secret, const uint8_t* data, size_t len);
  Packet* createAnonDatagram(uint8_t type, const LocalIdentity& sender, const Identity& dest, const uint8_t* secret, const uint8_t* data, size_t data_len);
  Packet* createGroupDatagram(uint8_t type, const GroupChannel& channel, const uint8_t* data, size_t data_len);
  Packet* createAck(const uint8_t* ack, uint8_t len);
  Packet* createAck(uint32_t ack_crc) { return createAck((uint8_t *) &ack_crc, 4); }
  Packet* createMultiAck(const uint8_t* ack, uint8_t len, uint8_t remaining);
  Packet* createMultiAck(uint32_t ack_crc, uint8_t remaining) { return createMultiAck((uint8_t *)&ack_crc, 4, remaining); }
  Packet* createPathReturn(const uint8_t* dest_hash, const uint8_t* secret, const uint8_t* path, uint8_t path_len, uint8_t extra_type, const uint8_t*extra, size_t extra_len);
  Packet* createPathReturn(const Identity& dest, const uint8_t* secret, const uint8_t* path, uint8_t path_len, uint8_t extra_type, const uint8_t*extra, size_t extra_len);
  Packet* createRawData(const uint8_t* data, size_t len);
  Packet* createTrace(uint32_t tag, uint32_t auth_code, uint8_t flags = 0);
  Packet* createControlData(const uint8_t* data, size_t len);

  /**
   * \brief  send a locally-generated Packet with flood routing
  */
  void sendFlood(Packet* packet, uint32_t delay_millis=0, uint8_t path_hash_size=1);

  /**
   * \brief  send a locally-generated Packet with flood routing
   * \param transport_codes   array of 2 codes to attach to packet
  */
  void sendFlood(Packet* packet, uint16_t* transport_codes, uint32_t delay_millis=0, uint8_t path_hash_size=1);

  /**
   * \brief  send a locally-generated Packet with Direct routing
  */
  void sendDirect(Packet* packet, const uint8_t* path, uint8_t path_len, uint32_t delay_millis=0);

  /**
   * \brief  send a locally-generated Packet to just neighbor nodes (zero hops)
  */
  void sendZeroHop(Packet* packet, uint32_t delay_millis=0);

  /**
   * \brief  send a locally-generated Packet to just neighbor nodes (zero hops), with specific transport codes
   * \param transport_codes   array of 2 codes to attach to packet
  */
  void sendZeroHop(Packet* packet, uint16_t* transport_codes, uint32_t delay_millis=0);

};

}
