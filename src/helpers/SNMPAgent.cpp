#ifdef WITH_SNMP

#include "SNMPAgent.h"
#include <esp_heap_caps.h>

#define SNMP_PORT 161

MeshSNMPAgent::MeshSNMPAgent()
  : _snmp("public"),
    _running(false),
    _uptime_secs(0),
    _packets_recv(0), _packets_sent(0), _recv_errors(0),
    _noise_floor(0), _last_rssi(0), _last_snr(0),
    _sent_flood(0), _sent_direct(0), _recv_flood(0), _recv_direct(0),
    _total_air_time_secs(0),
    _mqtt_connected_slots(0), _mqtt_queue_depth(0), _mqtt_skipped_publishes(0),
    _free_heap(0), _max_alloc(0), _internal_free(0), _psram_free(0),
    _wifi_rssi(0)
{
  _firmware_version[0] = '\0';
  _node_name[0] = '\0';
}

void MeshSNMPAgent::begin(const char* community) {
  if (_running) return;

  _snmp = SNMPAgent(community);
  _snmp.setUDP(&_udp);
  _snmp.begin();

  // System group (.1.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".1.1.0", &_uptime_secs);
  _snmp.addReadOnlyStaticStringHandler(MESHCORE_OID_BASE ".1.2.0", _firmware_version, sizeof(_firmware_version));
  _snmp.addReadOnlyStaticStringHandler(MESHCORE_OID_BASE ".1.3.0", _node_name, sizeof(_node_name));

  // Radio group (.2.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.1.0", &_packets_recv);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.2.0", &_packets_sent);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.3.0", &_recv_errors);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.4.0", &_noise_floor);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.5.0", &_last_rssi);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.6.0", &_last_snr);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.7.0", &_sent_flood);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.8.0", &_sent_direct);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.9.0", &_recv_flood);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.10.0", &_recv_direct);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".2.11.0", &_total_air_time_secs);

  // MQTT group (.3.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".3.1.0", &_mqtt_connected_slots);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".3.2.0", &_mqtt_queue_depth);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".3.3.0", &_mqtt_skipped_publishes);

  // Memory group (.4.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.1.0", &_free_heap);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.2.0", &_max_alloc);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.3.0", &_internal_free);
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".4.4.0", &_psram_free);

  // Network group (.5.x.0)
  _snmp.addIntegerHandler(MESHCORE_OID_BASE ".5.1.0", &_wifi_rssi);

  _snmp.sortHandlers();
  _running = true;
}

void MeshSNMPAgent::loop() {
  if (!_running) return;

  // Update memory and network stats locally (we're on Core 0 with WiFi)
  _free_heap = (int)ESP.getFreeHeap();
  _max_alloc = (int)ESP.getMaxAllocHeap();
  _internal_free = (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#ifdef BOARD_HAS_PSRAM
  _psram_free = (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
  _psram_free = 0;
#endif

  if (WiFi.isConnected()) {
    _wifi_rssi = (int)WiFi.RSSI();
  }

  _snmp.loop();
}

void MeshSNMPAgent::updateRadioStats(
    uint32_t packets_recv, uint32_t packets_sent, uint32_t recv_errors,
    int16_t noise_floor, int16_t last_rssi, int16_t last_snr,
    uint32_t sent_flood, uint32_t sent_direct,
    uint32_t recv_flood, uint32_t recv_direct,
    uint32_t total_air_time_secs, uint32_t uptime_secs) {
  _packets_recv = (int)packets_recv;
  _packets_sent = (int)packets_sent;
  _recv_errors = (int)recv_errors;
  _noise_floor = (int)noise_floor;
  _last_rssi = (int)last_rssi;
  _last_snr = (int)last_snr;
  _sent_flood = (int)sent_flood;
  _sent_direct = (int)sent_direct;
  _recv_flood = (int)recv_flood;
  _recv_direct = (int)recv_direct;
  _total_air_time_secs = (int)total_air_time_secs;
  _uptime_secs = (int)uptime_secs;
}

void MeshSNMPAgent::updateMQTTStats(int connected_slots, int queue_depth, int skipped_publishes) {
  _mqtt_connected_slots = connected_slots;
  _mqtt_queue_depth = queue_depth;
  _mqtt_skipped_publishes = skipped_publishes;
}

void MeshSNMPAgent::setNodeName(const char* name) {
  strncpy(_node_name, name, sizeof(_node_name) - 1);
  _node_name[sizeof(_node_name) - 1] = '\0';
}

void MeshSNMPAgent::setFirmwareVersion(const char* version) {
  strncpy(_firmware_version, version, sizeof(_firmware_version) - 1);
  _firmware_version[sizeof(_firmware_version) - 1] = '\0';
}

#endif // WITH_SNMP
