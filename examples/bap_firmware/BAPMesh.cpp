#include "BAPConfig.h"
#include "MyMesh.h"

BAPMesh::BAPMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
  : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(8), tables),
    _fs(nullptr), last_millis(0), uptime_millis(0) {
  memset(&_config, 0, sizeof(_config));
}

void BAPMesh::begin(FILESYSTEM* fs) {
  _fs = fs;
  loadConfig();
  mesh::Mesh::begin();
}

bool BAPMesh::loadConfig() {
  if (!_fs) return false;

  File f = _fs->open(BAP_CONFIG_FILE, "r");
  if (!f) {
    MESH_DEBUG_PRINTLN("No BAP config found, using defaults");
    memset(&_config, 0, sizeof(_config));
    _config.node_role = BAP_ROLE_AUTO;
    return false;
  }

  size_t bytes_read = f.read((uint8_t*)&_config, sizeof(_config));
  f.close();

  if (bytes_read == sizeof(_config)) {
    MESH_DEBUG_PRINTLN("BAP config loaded");
    return true;
  }
  return false;
}

bool BAPMesh::saveConfig() {
  if (!_fs) return false;

  File f = _fs->open(BAP_CONFIG_FILE, "w");
  if (!f) {
    MESH_DEBUG_PRINTLN("Failed to open config for writing");
    return false;
  }

  size_t bytes_written = f.write((const uint8_t*)&_config, sizeof(_config));
  f.close();

  return bytes_written == sizeof(_config);
}

void BAPMesh::sendArrivals(const BusArrival* arrivals, int count, uint32_t sequence) {
  if (count <= 0 || count > 5) return;

  BAPArrivalPacket pkt;
  memset(&pkt, 0, sizeof(pkt));

  pkt.packet_type = 0x01;
  pkt.count = (uint8_t)count;
  pkt.sequence = (uint16_t)sequence;
  pkt.generated_at = getRTCClock()->getCurrentTime();

  for (int i = 0; i < count && i < 5; i++) {
    pkt.arrivals[i] = arrivals[i];
  }

  // Calculate checksum
  pkt.checksum = calcChecksum((uint8_t*)&pkt, sizeof(pkt) - 1);

  // Create and send packet
  mesh::Packet* mesh_pkt = createRawData((uint8_t*)&pkt, sizeof(pkt));
  if (mesh_pkt) {
    sendFlood(mesh_pkt);
    MESH_DEBUG_PRINTLN("Sent %d arrivals, seq %u", count, sequence);
  }
}

void BAPMesh::onRawDataRecv(mesh::Packet* packet) {
  if (!packet) return;

  uint8_t* data = packet->payload;
  size_t len = packet->payload_len;

  // Check minimum size
  if (len < sizeof(BAPArrivalPacket)) {
    MESH_DEBUG_PRINTLN("BAP packet too small: %d", len);
    return;
  }

  BAPArrivalPacket* bap_pkt = (BAPArrivalPacket*)data;

  // Verify packet type
  if (bap_pkt->packet_type != 0x01) {
    MESH_DEBUG_PRINTLN("Unknown BAP packet type: %d", bap_pkt->packet_type);
    return;
  }

  // Verify checksum
  uint8_t calc = calcChecksum(data, sizeof(BAPArrivalPacket) - 1);
  if (calc != bap_pkt->checksum) {
    MESH_DEBUG_PRINTLN("BAP packet checksum mismatch");
    return;
  }

  // Verify count
  if (bap_pkt->count > 5) {
    MESH_DEBUG_PRINTLN("BAP packet invalid count: %d", bap_pkt->count);
    return;
  }

  MESH_DEBUG_PRINTLN("Received %d arrivals from mesh", bap_pkt->count);

  // Call the callback if set
  if (onArrivalsReceived) {
    onArrivalsReceived(bap_pkt->arrivals, bap_pkt->count, bap_pkt->generated_at);
  }
}

bool BAPMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  // Let all packets through - we want to receive BAP data
  return false;
}

bool BAPMesh::allowPacketForward(const mesh::Packet* packet) {
  // If repeater mode is enabled, forward all packets
  if (_config.is_repeater) {
    return true;
  }

  // If not a repeater, only forward BAP arrival packets
  if (packet->getPayloadType() == PAYLOAD_TYPE_RAW_CUSTOM) {
    return true;
  }

  // Don't forward other packets
  return false;
}

uint8_t BAPMesh::calcChecksum(const uint8_t* data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum ^= data[i];  // Simple XOR checksum
  }
  return sum;
}
