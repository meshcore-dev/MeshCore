#pragma once

#include "MeshCore.h"
#include "helpers/bridges/BridgeBase.h"
#include <PsychicMqttClient.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include "helpers/JWTHelper.h"

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#endif

#if defined(MQTT_DEBUG) && defined(ARDUINO)
  #include <Arduino.h>
  // USB CDC-aware debug macros: only print if Serial is ready (non-blocking check)
  // Serial.availableForWrite() returns bytes available in write buffer (>0 means ready)
  // This prevents hangs when USB CDC isn't ready yet (e.g., ESP32-S3 native USB)
  #define MQTT_DEBUG_PRINT(F, ...) do { if (Serial.availableForWrite() > 0) { Serial.printf("MQTT: " F, ##__VA_ARGS__); } } while(0)
  #define MQTT_DEBUG_PRINTLN(F, ...) do { if (Serial.availableForWrite() > 0) { Serial.printf("MQTT: " F "\n", ##__VA_ARGS__); } } while(0)
#else
  #define MQTT_DEBUG_PRINT(...) {}
  #define MQTT_DEBUG_PRINTLN(...) {}
#endif

#ifdef WITH_MQTT_BRIDGE

/**
 * @brief Bridge implementation using MQTT protocol for packet transport
 *
 * This bridge enables mesh packet transport over MQTT, allowing repeaters to
 * uplink packet data to multiple MQTT brokers for monitoring and analysis.
 *
 * Features:
 * - Multiple MQTT broker support
 * - Automatic reconnection with exponential backoff
 * - JSON message formatting for status, packets, and raw data
 * - Configurable topics and QoS levels
 * - Packet queuing during connection issues
 *
 * Message Types:
 * - Status: Device connection status and metadata
 * - Packets: Full packet data with RF characteristics
 * - Raw: Minimal raw packet data for map integration
 *
 * Configuration:
 * - Define WITH_MQTT_BRIDGE to enable this bridge
 * - Configure brokers via CLI commands
 * - Set origin name and IATA code for topic structure
 */
class MQTTBridge : public BridgeBase {
private:
  PsychicMqttClient* _mqtt_client;
  
  // MQTT broker configuration
  struct MQTTBroker {
    char host[64];
    uint16_t port;
    char username[32];
    char password[64];
    char client_id[32];
    uint8_t qos;
    bool enabled;
    bool connected;
    unsigned long last_attempt;
    unsigned long reconnect_interval;
  };
  
  static const int MAX_MQTT_BROKERS_COUNT = 3;
  MQTTBroker _brokers[MAX_MQTT_BROKERS_COUNT];
  int _active_brokers;
  
  // Message configuration
  char _origin[32];
  char _iata[8];
  char _device_id[65];  // Device public key (hex string)
  char _firmware_version[64];  // Firmware version string
  char _board_model[64];  // Board model string
  char _build_date[32];  // Build date string
  bool _status_enabled;
  bool _packets_enabled;
  bool _raw_enabled;
  bool _tx_enabled;
  unsigned long _last_status_publish;
  unsigned long _status_interval;
  
  // Packet queue for offline scenarios
  struct QueuedPacket {
    mesh::Packet* packet;
    unsigned long timestamp;
    bool is_tx;
    // Store raw radio data with each packet to avoid it being overwritten
    uint8_t raw_data[256];
    int raw_len;
    float snr;
    float rssi;
    bool has_raw_data;
  };
  
  static const int MAX_QUEUE_SIZE = 10;
  
  // FreeRTOS queue for thread-safe packet queuing
  #ifdef ESP_PLATFORM
  QueueHandle_t _packet_queue_handle;
  TaskHandle_t _mqtt_task_handle;
  SemaphoreHandle_t _raw_data_mutex;  // Mutex for raw radio data
  #else
  // Fallback to circular buffer for non-ESP32 platforms
  QueuedPacket _packet_queue[MAX_QUEUE_SIZE];
  int _queue_head;
  int _queue_tail;
  #endif
  int _queue_count;  // Protected by queue operations or mutex
  
  // NTP time sync
  WiFiUDP _ntp_udp;
  NTPClient _ntp_client;
  unsigned long _last_ntp_sync;
  bool _ntp_synced;
  bool _ntp_sync_pending;  // Flag to trigger NTP sync from loop() instead of event handler
  
  // Timezone handling
  Timezone* _timezone;
  
  // Raw radio data storage
  uint8_t _last_raw_data[256];
  int _last_raw_len;
  float _last_snr;
  float _last_rssi;
  unsigned long _last_raw_timestamp;
  
  // Let's Mesh Analyzer support
  bool _analyzer_us_enabled;
  bool _analyzer_eu_enabled;
  char _auth_token_us[768]; // JWT token for US server authentication (increased for owner/client fields)
  char _auth_token_eu[768]; // JWT token for EU server authentication (increased for owner/client fields)
  char _analyzer_username[70]; // Username in format v1_{UPPERCASE_PUBLIC_KEY}
  
  // Token expiration tracking
  unsigned long _token_us_expires_at;
  unsigned long _token_eu_expires_at;
  
  // Memory pressure monitoring
  unsigned long _last_memory_check;
  int _skipped_publishes;  // Count of skipped publishes due to memory pressure
  unsigned long _last_token_renewal_attempt_us;
  unsigned long _last_token_renewal_attempt_eu;
  unsigned long _last_reconnect_attempt_us;
  unsigned long _last_reconnect_attempt_eu;
  
  // Status publish retry tracking
  unsigned long _last_status_retry;  // Track last retry attempt (separate from successful publish)
  static const unsigned long STATUS_RETRY_INTERVAL = 30000; // Retry every 30 seconds if failed
  
  // Device identity for JWT token creation
  mesh::LocalIdentity *_identity;
  
  // PsychicMqttClient instances for different brokers
  PsychicMqttClient* _analyzer_us_client;
  PsychicMqttClient* _analyzer_eu_client;
  
  // Configuration validation state
  bool _config_valid;
  
  // Cached broker connection status (updated in callbacks to avoid redundant checks)
  bool _cached_has_brokers;
  bool _cached_has_analyzer_servers;
  
  // Throttle logging for disconnected broker messages
  unsigned long _last_no_broker_log;
  static const unsigned long NO_BROKER_LOG_INTERVAL = 30000; // Log every 30 seconds max
  
  // Throttle logging for analyzer client disconnected messages
  unsigned long _last_analyzer_us_log;
  unsigned long _last_analyzer_eu_log;
  static const unsigned long ANALYZER_LOG_INTERVAL = 30000; // Log every 30 seconds max
  unsigned long _last_config_warning; // Throttle configuration mismatch warnings
  static const unsigned long CONFIG_WARNING_INTERVAL = 300000; // Log every 5 minutes max
  
  // Optional pointers for collecting stats internally (set by mesh if available)
  mesh::Dispatcher* _dispatcher;  // For air times and errors
  mesh::Radio* _radio;             // For noise floor
  mesh::MainBoard* _board;         // For battery voltage
  mesh::MillisecondClock* _ms;    // For uptime
  
  // Internal methods
  void connectToBrokers();
  void processPacketQueue();
  bool publishStatus();  // Returns true if status was successfully published
  
  // FreeRTOS task function (runs on Core 0)
  #ifdef ESP_PLATFORM
  static void mqttTask(void* parameter);
  void mqttTaskLoop();  // Main loop for MQTT task
  void initializeWiFiInTask();  // WiFi initialization moved to task
  #endif
  void publishPacket(mesh::Packet* packet, bool is_tx, 
                     const uint8_t* raw_data = nullptr, int raw_len = 0, 
                     float snr = 0.0f, float rssi = 0.0f);
  void publishRaw(mesh::Packet* packet);
  void queuePacket(mesh::Packet* packet, bool is_tx);
  void dequeuePacket();
  bool isAnyBrokerConnected();
  void setBrokerDefaults();
  void syncTimeWithNTP();
  Timezone* createTimezoneFromString(const char* tz_string);
  bool isMQTTConfigValid();
  void checkConfigurationMismatch(); // Check for bridge.source/mqtt.tx mismatch
  bool isIATAValid() const;  // Check if IATA code is configured
  
public:
  /**
   * Constructs an MQTTBridge instance
   *
   * @param prefs Node preferences for configuration settings
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   * @param identity Device identity for JWT token creation
   */
  MQTTBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc, mesh::LocalIdentity *identity);

  /**
   * Initializes the MQTT bridge
   *
   * - Sets up default broker configuration
   * - Initializes WiFi client
   * - Prepares MQTT clients for each broker
   */
  void begin() override;

  /**
   * Stops the MQTT bridge
   *
   * - Disconnects from all brokers
   * - Clears packet queue
   * - Releases resources
   */
  void end() override;

  /**
   * Checks if MQTT configuration is valid
   *
   * @return true if all required MQTT settings are properly configured
   */
  bool isConfigValid() const;

  /**
   * Static method to validate MQTT configuration from preferences
   *
   * @param prefs Node preferences containing MQTT settings
   * @return true if all required MQTT settings are properly configured
   */
  static bool isConfigValid(const NodePrefs* prefs);

  /**
   * Check if MQTT bridge is ready to operate (has WiFi credentials)
   *
   * @return true if WiFi credentials are configured and bridge can connect
   */
  bool isReady() const;

  /**
   * Main loop handler
   * - Maintains broker connections
   * - Processes packet queue
   * - Publishes status updates
   */
  void loop() override;

  /**
   * Called when a packet is received via mesh
   * Queues the packet for MQTT publishing if enabled
   *
   * @param packet The received mesh packet
   */
  void onPacketReceived(mesh::Packet *packet) override;

  /**
   * Called when a packet needs to be transmitted via MQTT
   * Publishes the packet to all connected brokers
   *
   * @param packet The mesh packet to transmit
   */
  void sendPacket(mesh::Packet *packet) override;

  /**
   * Configure MQTT broker settings
   *
   * @param broker_index Broker index (0-2)
   * @param host Broker hostname
   * @param port Broker port
   * @param username MQTT username
   * @param password MQTT password
   * @param enabled Whether broker is enabled
   */
  void setBroker(int broker_index, const char* host, uint16_t port, 
                 const char* username, const char* password, bool enabled);

  /**
   * Set device origin name for MQTT topics
   *
   * @param origin Device name
   */
  void setOrigin(const char* origin);

  /**
   * Set IATA code for MQTT topics
   *
   * @param iata Airport code
   */
  void setIATA(const char* iata);

  /**
   * Set device public key for MQTT topics
   *
   * @param device_id Device public key (hex string)
   */
  void setDeviceID(const char* device_id);

  /**
   * Set firmware version for status messages
   *
   * @param firmware_version Firmware version string
   */
  void setFirmwareVersion(const char* firmware_version);

  /**
   * Set board model for status messages
   *
   * @param board_model Board model string
   */
  void setBoardModel(const char* board_model);

  /**
   * Set build date for client version
   *
   * @param build_date Build date string
   */
  void setBuildDate(const char* build_date);

  /**
   * Stores raw radio data for MQTT messages
   *
   * @param raw_data Raw radio transmission data
   * @param len Length of raw data
   * @param snr Signal-to-noise ratio
   * @param rssi Received signal strength indicator
   */
  void storeRawRadioData(const uint8_t* raw_data, int len, float snr, float rssi);
  
  // Let's Mesh Analyzer methods
  void setupAnalyzerServers();
  bool createAuthToken();
  bool publishToAnalyzerServers(const char* topic, const char* payload, bool retained = false);  // Returns true if at least one publish succeeded
  
  // PsychicMqttClient WebSocket methods
  void setupAnalyzerClients();
  void maintainAnalyzerConnections();
  bool publishToAnalyzerClient(PsychicMqttClient* client, const char* topic, const char* payload, bool retained = false);  // Returns true if publish succeeded
  void publishStatusToAnalyzerClient(PsychicMqttClient* client, const char* server_name);
  
  /**
   * Optimize MQTT client configuration for memory efficiency
   * Reduces buffer sizes to minimize memory usage while maintaining functionality
   * 
   * @param client MQTT client to optimize
   * @param is_analyzer_client If true, uses larger buffer for JWT tokens (768 bytes)
   */
  void optimizeMqttClientConfig(PsychicMqttClient* client, bool is_analyzer_client = false);

  /**
   * Enable/disable message types
   *
   * @param status Enable status messages
   * @param packets Enable packet messages
   * @param raw Enable raw messages
   */
  void setMessageTypes(bool status, bool packets, bool raw);

  /**
   * Get connection status for all brokers
   *
   * @return Number of connected brokers
   */
  int getConnectedBrokers() const;

  /**
   * Get queue status
   *
   * @return Number of queued packets
   */
  int getQueueSize() const;

  /**
   * Set optional pointers for stats collection.
   * If these are set, stats will be collected automatically when publishing status.
   *
   * @param dispatcher Dispatcher (or Mesh*) for air times and errors
   * @param radio Radio for noise floor
   * @param board MainBoard for battery voltage
   * @param ms MillisecondClock for uptime
   */
  void setStatsSources(mesh::Dispatcher* dispatcher, mesh::Radio* radio, 
                       mesh::MainBoard* board, mesh::MillisecondClock* ms);

private:
  /**
   * Generate client version string in format "meshcore/{firmware_version}"
   * Memory-efficient: writes to provided buffer, no dynamic allocation
   *
   * @param buffer Buffer to write the client version string to
   * @param buffer_size Size of the buffer (must be at least 64 bytes)
   */
  void getClientVersion(char* buffer, size_t buffer_size) const;

  /**
   * Log memory status for debugging
   */
  void logMemoryStatus();
};

#endif
