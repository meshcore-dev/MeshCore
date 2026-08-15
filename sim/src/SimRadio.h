#pragma once
#include <Dispatcher.h>
#include <functional>
#include <vector>
#include <cstdint>
#include <cstring>

namespace sim {

// Simulated RF channel event — one node transmitting a raw packet
struct RFEvent {
  int      src_node;       // index of transmitting node
  uint8_t  bytes[255];
  int      len;
  float    tx_snr;         // SNR at transmitter (affects received SNR at each node)
  uint64_t tx_time_ms;     // absolute sim time of transmission
  uint32_t airtime_ms;     // how long the packet occupies the air
};

// Per-link RF model: determines whether node B receives a packet from node A,
// and with what SNR. Override to model different topologies.
struct RFChannelModel {
  virtual bool canReceive(int from_node, int to_node) = 0;
  virtual float receivedSNR(int from_node, int to_node, float base_snr) = 0;
  virtual ~RFChannelModel() = default;
};

// Simple model: all nodes within `range` of each other hear everything at constant SNR
struct FullMeshModel : public RFChannelModel {
  float snr;
  FullMeshModel(float snr = 8.0f) : snr(snr) {}
  bool canReceive(int from, int to) override { return from != to; }
  float receivedSNR(int from, int to, float) override { return snr; }
};

// Linear chain: node i only hears i-1 and i+1
struct ChainModel : public RFChannelModel {
  float snr;
  ChainModel(float snr = 8.0f) : snr(snr) {}
  bool canReceive(int from, int to) override { return abs(from - to) == 1; }
  float receivedSNR(int from, int to, float) override { return snr; }
};

// Distance-based model: nodes have x,y positions; signal drops with distance
struct PositionalModel : public RFChannelModel {
  struct NodePos { float x, y; };
  std::vector<NodePos> positions;
  float range;   // max range in arbitrary units
  float snr_at_range;
  float snr_peak;

  PositionalModel(float range = 1.0f, float snr_peak = 12.0f, float snr_at_range = 3.0f)
    : range(range), snr_at_range(snr_at_range), snr_peak(snr_peak) {}

  void addNode(float x, float y) { positions.push_back({x, y}); }

  bool canReceive(int from, int to) override {
    if (from == to || from >= (int)positions.size() || to >= (int)positions.size()) return false;
    float dx = positions[from].x - positions[to].x;
    float dy = positions[from].y - positions[to].y;
    return sqrtf(dx*dx + dy*dy) <= range;
  }

  float receivedSNR(int from, int to, float) override {
    float dx = positions[from].x - positions[to].x;
    float dy = positions[from].y - positions[to].y;
    float dist = sqrtf(dx*dx + dy*dy);
    float t = dist / range;
    return snr_peak + t * (snr_at_range - snr_peak);
  }
};

// -------------------------------------------------------------------------
// SimRadio: implements mesh::Radio for a simulated node.
// Receives packets via injectRecv(); transmits by calling the bus callback.
//
// Collision model: LoRa captures the strongest signal if SNR difference
// exceeds CAPTURE_THRESHOLD_DB (typically 6 dB). Below that, both packets
// are corrupted and the receiver gets nothing. This matches real LoRa
// behaviour far better than silent-drop on the second arrival.
// -------------------------------------------------------------------------
static constexpr float LORA_CAPTURE_THRESHOLD_DB = 6.0f;

class SimRadio : public mesh::Radio {
public:
  using TxCallback = std::function<void(int node_idx, const uint8_t*, int len, uint32_t airtime_ms, float tx_power_dbm)>;

  // TX power in dBm. Default 20 dBm = typical LoRa max.
  // Reducing this directly lowers received SNR at all receivers by the same delta.
  // LoRa SX1262 range: +14 to +22 dBm. SX1276: +2 to +20 dBm.
  float tx_power_dbm = 20.0f;

  SimRadio(int node_idx, TxCallback on_tx, float lora_bw_khz = 62.5f, int lora_sf = 8)
    : _node_idx(node_idx), _on_tx(on_tx), _bw(lora_bw_khz), _sf(lora_sf)
  {}

  // Called by SimBus to deliver a packet to this node.
  // If a packet is already pending:
  //   - stronger signal wins capture if SNR delta >= LORA_CAPTURE_THRESHOLD_DB
  //   - otherwise both are corrupted (collision) → _collision flagged, pending cleared
  void injectRecv(const uint8_t* bytes, int len, float snr, int sender_node = -1) {
    if (!_in_recv_mode) return;

    if (!_rx_pending && !_collision) {
      // Clean slot — accept this packet
      memcpy(_rx_buf, bytes, len);
      _rx_len    = len;
      _rx_snr    = snr;
      _rx_sender = sender_node;
      _rx_pending = true;
      return;
    }

    if (_collision) {
      // Already collided this tick — additional arrivals are also lost
      total_collisions++;
      return;
    }

    // Second arrival while first is pending — apply capture effect
    float delta = snr - _rx_snr;
    if (delta >= LORA_CAPTURE_THRESHOLD_DB) {
      // New signal captures — replace pending packet
      memcpy(_rx_buf, bytes, len);
      _rx_len    = len;
      _rx_snr    = snr;
      _rx_sender = sender_node;
      // _rx_pending stays true, _collision stays false
    } else if (-delta >= LORA_CAPTURE_THRESHOLD_DB) {
      // Existing signal captures — keep it, discard new arrival
    } else {
      // Too close in level — collision, both lost
      _rx_pending = false;
      _collision  = true;
      total_collisions++;
    }
  }

  // Reset collision flag at the start of each bus tick so the next
  // packet can be received cleanly.
  void tickReset() { _collision = false; }

  int  getLastSender()    const { return _rx_sender_last; }
  bool hadCollision()     const { return _collision; }
  uint32_t getCollisions()const { return total_collisions; }

  uint32_t total_collisions = 0;

  // mesh::Radio interface
  void begin() override { _in_recv_mode = true; }

  int recvRaw(uint8_t* bytes, int sz) override {
    if (!_rx_pending) return 0;
    int n = _rx_len < sz ? _rx_len : sz;
    memcpy(bytes, _rx_buf, n);
    _last_snr = _rx_snr;
    _last_rssi = -90.0f + _rx_snr;
    _rx_sender_last = _rx_sender;
    _rx_pending = false;
    return n;
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    // LoRa airtime model: symbols * symbol_time
    // symbol_time = 2^SF / BW
    float sym_time_ms = (float)(1 << _sf) / _bw;
    // preamble (8 syms) + ceil((8*len - 4*SF + 28 + 16) / (4*SF)) coding overhead * (CR+4)
    float payload_syms = ceilf((8.0f*len_bytes - 4.0f*_sf + 28.0f + 16.0f) / (4.0f*_sf)) * 5.0f;
    float total_syms = 12.25f + payload_syms;  // 8 preamble + 4.25 header
    return (uint32_t)(total_syms * sym_time_ms);
  }

  float packetScore(float snr, int packet_len) override {
    return snr / 10.0f;   // simplified score for simulation
  }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    uint32_t at = getEstAirtimeFor(len);
    _on_tx(_node_idx, bytes, len, at, tx_power_dbm);
    _in_recv_mode = false;
    _send_done = false;
    _send_done = true;   // instant in sim
    return true;
  }

  bool isSendComplete() override { return _send_done; }
  void onSendFinished() override { _in_recv_mode = true; }
  bool isInRecvMode() const override { return _in_recv_mode; }
  float getLastRSSI() const override { return _last_rssi; }
  float getLastSNR() const override { return _last_snr; }

private:
  int _node_idx;
  TxCallback _on_tx;
  float _bw;
  int   _sf;

  uint8_t _rx_buf[255];
  int     _rx_len = 0;
  float   _rx_snr = 0;
  bool    _rx_pending = false;
  bool    _collision  = false;
  bool    _in_recv_mode = false;
  bool    _send_done = true;
  float   _last_snr = 0, _last_rssi = 0;
  int     _rx_sender = -1, _rx_sender_last = -1;
};

}
