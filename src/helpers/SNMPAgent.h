#pragma once

#ifdef WITH_SNMP

#include <WiFi.h>
#include <WiFiUdp.h>
#include <SNMP_Agent.h>

// Temporary private enterprise OID base — replace with registered PEN when available.
// All MeshCore OIDs live under this subtree.
#define MESHCORE_OID_BASE ".1.3.6.1.4.1.99999"

// OID layout:
//   .1.x.0  = system   (uptime, version, node name)
//   .2.x.0  = radio    (packets, RSSI, SNR, noise floor, air time)
//   .3.x.0  = mqtt     (connected slots, queue depth, skipped publishes)
//   .4.x.0  = memory   (free heap, max alloc, internal free, PSRAM free)
//   .5.x.0  = network  (WiFi RSSI)

class MeshSNMPAgent {
public:
  MeshSNMPAgent();
  void begin(const char* community);
  void loop();

  // Called from the mesh task (Core 1) to push fresh stats into SNMP-visible variables.
  // Copies are atomic for 32-bit aligned ints on ESP32, so no mutex needed.
  void updateRadioStats(uint32_t packets_recv, uint32_t packets_sent, uint32_t recv_errors,
                        int16_t noise_floor, int16_t last_rssi, int16_t last_snr,
                        uint32_t sent_flood, uint32_t sent_direct,
                        uint32_t recv_flood, uint32_t recv_direct,
                        uint32_t total_air_time_secs, uint32_t uptime_secs);

  void updateMQTTStats(int connected_slots, int queue_depth, int skipped_publishes);

  void setNodeName(const char* name);
  void setFirmwareVersion(const char* version);

  bool isRunning() const { return _running; }

private:
  WiFiUDP _udp;
  SNMPAgent _snmp;
  bool _running;

  // System OIDs
  int _uptime_secs;
  char _firmware_version[32];
  char _node_name[32];

  // Radio OIDs
  int _packets_recv;
  int _packets_sent;
  int _recv_errors;
  int _noise_floor;
  int _last_rssi;
  int _last_snr;
  int _sent_flood;
  int _sent_direct;
  int _recv_flood;
  int _recv_direct;
  int _total_air_time_secs;

  // MQTT OIDs
  int _mqtt_connected_slots;
  int _mqtt_queue_depth;
  int _mqtt_skipped_publishes;

  // Memory OIDs (updated in loop() since we're on Core 0 with WiFi)
  int _free_heap;
  int _max_alloc;
  int _internal_free;
  int _psram_free;

  // Network OIDs
  int _wifi_rssi;
};

#endif // WITH_SNMP
