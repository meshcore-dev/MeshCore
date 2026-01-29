#include "MQTTBridge.h"
#include "../MQTTMessageBuilder.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h>

#ifdef ESP_PLATFORM
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#endif

// Helper function to strip quotes from strings (both single and double quotes)
static void stripQuotes(char* str, size_t max_len) {
  if (!str || max_len == 0) return;
  
  size_t len = strlen(str);
  if (len == 0) return;
  
  // Remove leading quote (single or double)
  if (str[0] == '"' || str[0] == '\'') {
    memmove(str, str + 1, len);
    len--;
  }
  
  // Remove trailing quote (single or double)
  if (len > 0 && (str[len-1] == '"' || str[len-1] == '\'')) {
    str[len-1] = '\0';
  }
}

// Helper function to check if WiFi credentials are valid
static bool isWiFiConfigValid(const NodePrefs* prefs) {
  // Check if WiFi SSID is configured (not empty)
  if (strlen(prefs->wifi_ssid) == 0) {
    return false;
  }
  
  // WiFi password can be empty for open networks, so we don't check it
  
  return true;
}

#ifdef WITH_MQTT_BRIDGE

MQTTBridge::MQTTBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc, mesh::LocalIdentity *identity)
    : BridgeBase(prefs, mgr, rtc), _mqtt_client(nullptr),
      _active_brokers(0), _queue_count(0),
      _last_status_publish(0), _last_status_retry(0), _status_interval(300000), // 5 minutes default
              _ntp_client(_ntp_udp, "pool.ntp.org", 0, 60000), _last_ntp_sync(0), _ntp_synced(false), _ntp_sync_pending(false),
              _timezone(nullptr), _last_raw_len(0), _last_snr(0), _last_rssi(0), _last_raw_timestamp(0),
              _analyzer_us_enabled(false), _analyzer_eu_enabled(false), _identity(identity),
              _analyzer_us_client(nullptr), _analyzer_eu_client(nullptr), _config_valid(false),
              _cached_has_brokers(false), _cached_has_analyzer_servers(false),
              _last_memory_check(0), _skipped_publishes(0),
              _last_no_broker_log(0), _last_config_warning(0), _dispatcher(nullptr), _radio(nullptr), _board(nullptr), _ms(nullptr)
#ifdef ESP_PLATFORM
              , _packet_queue_handle(nullptr), _mqtt_task_handle(nullptr), _raw_data_mutex(nullptr)
#else
              , _queue_head(0), _queue_tail(0)
#endif
{
  
  // Initialize default values
  strncpy(_origin, "MeshCore-Repeater", sizeof(_origin) - 1);
  strncpy(_iata, "XXX", sizeof(_iata) - 1);
  strncpy(_device_id, "DEVICE_ID_PLACEHOLDER", sizeof(_device_id) - 1);
  strncpy(_firmware_version, "unknown", sizeof(_firmware_version) - 1);
  strncpy(_board_model, "unknown", sizeof(_board_model) - 1);
  strncpy(_build_date, "unknown", sizeof(_build_date) - 1);
  _status_enabled = true;
  _packets_enabled = true;
  _raw_enabled = false;
  _tx_enabled = false;  // Disable TX packets by default
  
  // Initialize MQTT server settings with defaults (empty/null values)
  _prefs->mqtt_server[0] = '\0';  // Empty string
  _prefs->mqtt_port = 0;          // Invalid port
  _prefs->mqtt_username[0] = '\0'; // Empty string
  _prefs->mqtt_password[0] = '\0'; // Empty string
  
  // Override with build flags if defined
#ifdef MQTT_SERVER
  strncpy(_prefs->mqtt_server, MQTT_SERVER, sizeof(_prefs->mqtt_server) - 1);
#endif
#ifdef MQTT_PORT
  _prefs->mqtt_port = MQTT_PORT;
#endif
#ifdef MQTT_USERNAME
  strncpy(_prefs->mqtt_username, MQTT_USERNAME, sizeof(_prefs->mqtt_username) - 1);
#endif
#ifdef MQTT_PASSWORD
  strncpy(_prefs->mqtt_password, MQTT_PASSWORD, sizeof(_prefs->mqtt_password) - 1);
#endif
  
  // Initialize packet queue (FreeRTOS queue will be created in begin())
  #ifdef ESP_PLATFORM
  // Queue and mutex will be created in begin()
  #else
  // Initialize circular buffer for non-ESP32 platforms
  memset(_packet_queue, 0, sizeof(_packet_queue));
  for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
    _packet_queue[i].has_raw_data = false;
  }
  #endif
  
  // Initialize throttle log timers
  _last_no_broker_log = 0;
  _last_analyzer_us_log = 0;
  _last_analyzer_eu_log = 0;
  
  // Set default broker configuration
  setBrokerDefaults();
}

void MQTTBridge::begin() {
  MQTT_DEBUG_PRINTLN("Initializing MQTT Bridge...");
  
  // Check if WiFi credentials are configured first
  if (!isWiFiConfigValid(_prefs)) {
    MQTT_DEBUG_PRINTLN("MQTT Bridge initialization skipped - WiFi credentials not configured");
    return;
  }
  
  // Validate custom MQTT broker configuration (optional)
  _config_valid = isMQTTConfigValid();
  if (!_config_valid) {
    MQTT_DEBUG_PRINTLN("No valid custom MQTT server configured - analyzer servers will still work");
  } else {
    MQTT_DEBUG_PRINTLN("Custom MQTT server configuration is valid");
  }
  
  // Update origin and IATA from preferences
  strncpy(_origin, _prefs->mqtt_origin, sizeof(_origin) - 1);
  _origin[sizeof(_origin) - 1] = '\0';
  strncpy(_iata, _prefs->mqtt_iata, sizeof(_iata) - 1);
  _iata[sizeof(_iata) - 1] = '\0';
  
  // Strip quotes from MQTT server configuration if present
  stripQuotes(_prefs->mqtt_server, sizeof(_prefs->mqtt_server));
  stripQuotes(_prefs->mqtt_username, sizeof(_prefs->mqtt_username));
  stripQuotes(_prefs->mqtt_password, sizeof(_prefs->mqtt_password));
  
  // Strip quotes from origin and IATA if present
  stripQuotes(_origin, sizeof(_origin));
  stripQuotes(_iata, sizeof(_iata));
  
  // Convert IATA code to uppercase (IATA codes are conventionally uppercase)
  for (int i = 0; _iata[i]; i++) {
    _iata[i] = toupper(_iata[i]);
  }
  
  // Update enabled flags from preferences
  _status_enabled = _prefs->mqtt_status_enabled;
  _packets_enabled = _prefs->mqtt_packets_enabled;
  _raw_enabled = _prefs->mqtt_raw_enabled;
  _tx_enabled = _prefs->mqtt_tx_enabled;
  // Set status interval to 5 minutes (300000 ms), or use preference if set and valid
  if (_prefs->mqtt_status_interval >= 1000 && _prefs->mqtt_status_interval <= 3600000) {
    _status_interval = _prefs->mqtt_status_interval;
  } else {
    // Invalid or uninitialized value - fix it in preferences and use default
    _prefs->mqtt_status_interval = 300000; // Fix the preference value
    _status_interval = 300000; // 5 minutes default
  }
  
  // Check for configuration mismatch: bridge.source=tx but mqtt.tx=off
  checkConfigurationMismatch();
  
  MQTT_DEBUG_PRINTLN("Config: Origin=%s, IATA=%s, Device=%s", _origin, _iata, _device_id);
  
  #ifdef ESP_PLATFORM
  // Create FreeRTOS queue for thread-safe packet queuing
  _packet_queue_handle = xQueueCreate(MAX_QUEUE_SIZE, sizeof(QueuedPacket));
  if (_packet_queue_handle == nullptr) {
    MQTT_DEBUG_PRINTLN("Failed to create packet queue!");
    return;
  }
  
  // Create mutex for raw radio data protection
  _raw_data_mutex = xSemaphoreCreateMutex();
  if (_raw_data_mutex == nullptr) {
    MQTT_DEBUG_PRINTLN("Failed to create raw data mutex!");
    vQueueDelete(_packet_queue_handle);
    _packet_queue_handle = nullptr;
    return;
  }
  
  // Initialize PsychicMqttClient (will be used by task)
  _mqtt_client = new PsychicMqttClient();
  
  // Optimize MQTT client configuration for memory efficiency
  optimizeMqttClientConfig(_mqtt_client, false);
  
  // Set up event callbacks for the main MQTT client
  _mqtt_client->onConnect([this](bool sessionPresent) {
    MQTT_DEBUG_PRINTLN("MQTT broker connected");
    for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
      if (_brokers[i].enabled && !_brokers[i].connected) {
        _brokers[i].connected = true;
        _active_brokers++;
        // Update cached broker status
        _cached_has_brokers = isAnyBrokerConnected();
        break;
      }
    }
  });
  
  _mqtt_client->onDisconnect([this](bool sessionPresent) {
    MQTT_DEBUG_PRINTLN("MQTT broker disconnected");
    for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
      if (_brokers[i].connected) {
        _brokers[i].connected = false;
        _active_brokers--;
        // Update cached broker status
        _cached_has_brokers = isAnyBrokerConnected();
        break;
      }
    }
  });
  
  // Set default broker from preferences or build flags
  setBroker(0, _prefs->mqtt_server, _prefs->mqtt_port, _prefs->mqtt_username, _prefs->mqtt_password, true);
  
  // Setup Let's Mesh Analyzer servers configuration
  _analyzer_us_enabled = _prefs->mqtt_analyzer_us_enabled;
  _analyzer_eu_enabled = _prefs->mqtt_analyzer_eu_enabled;
  MQTT_DEBUG_PRINTLN("Analyzer servers - US: %s, EU: %s", 
                     _analyzer_us_enabled ? "enabled" : "disabled",
                     _analyzer_eu_enabled ? "enabled" : "disabled");
  
  // Create FreeRTOS task for MQTT/WiFi processing on Core 0
  #ifndef MQTT_TASK_CORE
  #define MQTT_TASK_CORE 0
  #endif
  #ifndef MQTT_TASK_STACK_SIZE
  #define MQTT_TASK_STACK_SIZE 8192  // Reverted: 6144 was too small, caused boot loop after NTP sync
  #endif
  #ifndef MQTT_TASK_PRIORITY
  #define MQTT_TASK_PRIORITY 1
  #endif
  
  BaseType_t task_result = xTaskCreatePinnedToCore(
    mqttTask,           // Task function
    "MQTTBridge",      // Task name
    MQTT_TASK_STACK_SIZE,  // Stack size
    this,               // Parameter (this pointer)
    MQTT_TASK_PRIORITY, // Priority
    &_mqtt_task_handle, // Task handle
    MQTT_TASK_CORE      // Core ID (0)
  );
  
  if (task_result != pdPASS) {
    MQTT_DEBUG_PRINTLN("Failed to create MQTT task!");
    vQueueDelete(_packet_queue_handle);
    _packet_queue_handle = nullptr;
    vSemaphoreDelete(_raw_data_mutex);
    _raw_data_mutex = nullptr;
    delete _mqtt_client;
    _mqtt_client = nullptr;
    return;
  }
  
  MQTT_DEBUG_PRINTLN("MQTT task created on Core %d", MQTT_TASK_CORE);
  #else
  // Non-ESP32: Initialize WiFi directly (no task)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setAutoConnect(true);
  WiFi.begin(_prefs->wifi_ssid, _prefs->wifi_password);
  
  // Initialize PsychicMqttClient
  _mqtt_client = new PsychicMqttClient();
  optimizeMqttClientConfig(_mqtt_client, false);
  
  // Set up event callbacks
  _mqtt_client->onConnect([this](bool sessionPresent) {
    MQTT_DEBUG_PRINTLN("MQTT broker connected");
    for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
      if (_brokers[i].enabled && !_brokers[i].connected) {
        _brokers[i].connected = true;
        _active_brokers++;
        // Update cached broker status
        _cached_has_brokers = isAnyBrokerConnected();
        break;
      }
    }
  });
  
  _mqtt_client->onDisconnect([this](bool sessionPresent) {
    MQTT_DEBUG_PRINTLN("MQTT broker disconnected");
    for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
      if (_brokers[i].connected) {
        _brokers[i].connected = false;
        _active_brokers--;
        // Update cached broker status
        _cached_has_brokers = isAnyBrokerConnected();
        break;
      }
    }
  });
  
  setBroker(0, _prefs->mqtt_server, _prefs->mqtt_port, _prefs->mqtt_username, _prefs->mqtt_password, true);
  _analyzer_us_enabled = _prefs->mqtt_analyzer_us_enabled;
  _analyzer_eu_enabled = _prefs->mqtt_analyzer_eu_enabled;
  setupAnalyzerClients();
  connectToBrokers();
  #endif
  
  _initialized = true;
  MQTT_DEBUG_PRINTLN("MQTT Bridge initialized");
}

void MQTTBridge::end() {
  MQTT_DEBUG_PRINTLN("Stopping MQTT Bridge...");
  
  #ifdef ESP_PLATFORM
  // Delete FreeRTOS task first (it will clean up WiFi/MQTT connections)
  if (_mqtt_task_handle != nullptr) {
    vTaskDelete(_mqtt_task_handle);
    _mqtt_task_handle = nullptr;
    // Give task time to clean up
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  // Clean up queued packets from FreeRTOS queue
  if (_packet_queue_handle != nullptr) {
    QueuedPacket queued;
    while (xQueueReceive(_packet_queue_handle, &queued, 0) == pdTRUE) {
      if (queued.packet) {
        _mgr->free(queued.packet);
        queued.packet = nullptr;
      }
      _queue_count--;
    }
    vQueueDelete(_packet_queue_handle);
    _packet_queue_handle = nullptr;
  }
  
  // Delete mutex
  if (_raw_data_mutex != nullptr) {
    vSemaphoreDelete(_raw_data_mutex);
    _raw_data_mutex = nullptr;
  }
  #else
  // Disconnect from all brokers
  for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
    if (_brokers[i].enabled && _brokers[i].connected) {
      _mqtt_client->disconnect();
      _brokers[i].connected = false;
    }
  }
  
  // Disconnect analyzer clients
  if (_analyzer_us_client) {
    _analyzer_us_client->disconnect();
    delete _analyzer_us_client;
    _analyzer_us_client = nullptr;
  }
  if (_analyzer_eu_client) {
    _analyzer_eu_client->disconnect();
    delete _analyzer_eu_client;
    _analyzer_eu_client = nullptr;
  }
  
  // Clean up queued packets to prevent memory leaks
  for (int i = 0; i < _queue_count; i++) {
    int index = (_queue_head + i) % MAX_QUEUE_SIZE;
    if (_packet_queue[index].packet) {
      _mgr->free(_packet_queue[index].packet);
      _packet_queue[index].packet = nullptr;
    }
    memset(&_packet_queue[index], 0, sizeof(QueuedPacket));
  }
  
  _queue_count = 0;
  _queue_head = 0;
  _queue_tail = 0;
  memset(_packet_queue, 0, sizeof(_packet_queue));
  #endif
  
  // Clean up timezone object to prevent memory leak
  if (_timezone) {
    delete _timezone;
    _timezone = nullptr;
  }
  
  // Clean up resources
  if (_mqtt_client) {
    delete _mqtt_client;
    _mqtt_client = nullptr;
  }
  
  _initialized = false;
  MQTT_DEBUG_PRINTLN("MQTT Bridge stopped");
}

#ifdef ESP_PLATFORM
void MQTTBridge::mqttTask(void* parameter) {
  MQTTBridge* bridge = static_cast<MQTTBridge*>(parameter);
  if (bridge) {
    bridge->mqttTaskLoop();
  }
  // Task should never return, but if it does, delete itself
  vTaskDelete(nullptr);
}

void MQTTBridge::initializeWiFiInTask() {
  MQTT_DEBUG_PRINTLN("Initializing WiFi in MQTT task...");
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  
  // Enable automatic reconnection - ESP32 will handle reconnection automatically
  WiFi.setAutoReconnect(true);
  WiFi.setAutoConnect(true);
  
  // Set up WiFi event handlers for better diagnostics and immediate disconnection detection
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch(event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        MQTT_DEBUG_PRINTLN("WiFi connected: %s", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
        // Set flag to trigger NTP sync from loop() instead of doing it here
        if (!_ntp_synced && !_ntp_sync_pending) {
          _ntp_sync_pending = true;
        }
        break;
      default:
        break;
    }
  });
  
  WiFi.begin(_prefs->wifi_ssid, _prefs->wifi_password);
  
  // WiFi connection is asynchronous - don't block here
  // Auto-reconnect will handle connection in the background
  
  // Setup PsychicMqttClient WebSocket clients for analyzer servers
  setupAnalyzerClients();
  
  MQTT_DEBUG_PRINTLN("WiFi initialization started in task");
}

void MQTTBridge::mqttTaskLoop() {
  // Initialize WiFi first
  initializeWiFiInTask();
  
  // Wait a bit for WiFi to start connecting
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  // Main task loop
  while (true) {
    // Run the main MQTT bridge loop logic
    // This replaces the original loop() method but runs in the task
    
    // Actively monitor and manage WiFi connection
    static unsigned long last_wifi_check = 0;
    static unsigned long last_wifi_reconnect_attempt = 0;
    static wl_status_t last_wifi_status = WL_DISCONNECTED;
    static bool wifi_status_initialized = false;
    static unsigned long wifi_disconnected_time = 0;
    
    unsigned long now = millis();
    wl_status_t current_wifi_status = WiFi.status();
    
    // Initialize last_wifi_status on first loop() call
    if (!wifi_status_initialized) {
      last_wifi_status = current_wifi_status;
      wifi_status_initialized = true;
      // Don't sync here - let the pending flag or transition handler do it
    }
    
    // Check WiFi status every 10 seconds for faster detection
    if (now - last_wifi_check > 10000) {
      last_wifi_check = now;
      
      if (current_wifi_status == WL_CONNECTED) {
        if (last_wifi_status != WL_CONNECTED) {
          wifi_disconnected_time = 0;
          // Configure WiFi power management for efficient operation
          wifi_ps_type_t ps_mode;
          uint8_t ps_pref = _prefs->wifi_power_save;
          if (ps_pref == 1) {
            ps_mode = WIFI_PS_NONE;
          } else if (ps_pref == 2) {
            ps_mode = WIFI_PS_MAX_MODEM;
          } else {
            ps_mode = WIFI_PS_MIN_MODEM;
          }
          esp_wifi_set_ps(ps_mode);
          
          // Set WiFi TX power
          #ifdef MQTT_WIFI_TX_POWER
          WiFi.setTxPower(MQTT_WIFI_TX_POWER);
          #else
          WiFi.setTxPower(WIFI_POWER_11dBm);
          #endif
          
          // NTP sync will be handled by _ntp_sync_pending flag from WiFi event handler
          // This prevents multiple simultaneous syncs
        }
        last_wifi_status = WL_CONNECTED;
      } else {
        if (last_wifi_status == WL_CONNECTED) {
          wifi_disconnected_time = now;
        } else if (wifi_disconnected_time > 0) {
          unsigned long disconnected_duration = now - wifi_disconnected_time;
          
          // Try to force reconnection if disconnected for more than 30 seconds
          if (disconnected_duration > 30000 && (now - last_wifi_reconnect_attempt) > 30000) {
            last_wifi_reconnect_attempt = now;
            WiFi.disconnect();
            WiFi.begin(_prefs->wifi_ssid, _prefs->wifi_password);
          }
        }
        last_wifi_status = current_wifi_status;
      }
    }
    
    // Check for pending NTP sync (triggered from WiFi event handler)
    if (_ntp_sync_pending && WiFi.status() == WL_CONNECTED) {
      _ntp_sync_pending = false;
      syncTimeWithNTP();
    }
    
    // Check if analyzer server settings have changed in preferences
    static unsigned long last_analyzer_check = 0;
    if (now - last_analyzer_check > 5000) {
      last_analyzer_check = now;
      if (_analyzer_us_enabled != _prefs->mqtt_analyzer_us_enabled || 
          _analyzer_eu_enabled != _prefs->mqtt_analyzer_eu_enabled) {
        MQTT_DEBUG_PRINTLN("Analyzer settings changed - updating...");
        setupAnalyzerServers();
      }
    }
    
    // Maintain broker connections
    connectToBrokers();
    
    // Maintain analyzer server connections
    maintainAnalyzerConnections();
    
    // Process packet queue
    processPacketQueue();
    
    // Periodic configuration check (throttled to avoid spam)
    checkConfigurationMismatch();
    
    // Periodic NTP sync (every hour) - only when connected
    if (WiFi.status() == WL_CONNECTED && now - _last_ntp_sync > 3600000) {
      syncTimeWithNTP();
    }
    
    // Publish status updates (handle millis() overflow correctly)
    if (_status_enabled) {
      // Use cached destination status (updated in connection callbacks) - early exit if no destinations
      // Only refresh cache if status publish is enabled to avoid unnecessary checks
      bool has_custom_brokers = _cached_has_brokers && _config_valid;
      bool has_destinations = has_custom_brokers || _cached_has_analyzer_servers;
      
      // Early exit if no destinations - skip all the expensive logic below
      if (!has_destinations) {
        if (_last_status_retry != 0) {
          _last_status_retry = 0;
        }
      } else {
        bool should_publish = false;
        
        // First, check if we need to respect retry interval (prevents spam when publish keeps failing)
        if (_last_status_retry != 0) {
          unsigned long retry_elapsed = (now >= _last_status_retry) ?
                                       (now - _last_status_retry) :
                                       (ULONG_MAX - _last_status_retry + now + 1);
          if (retry_elapsed < STATUS_RETRY_INTERVAL) {
            // Too soon to retry - wait longer
            should_publish = false;
          } else {
            // Retry interval has passed - allow retry
            should_publish = true;
          }
        } else {
          // No pending retry - check if normal interval has passed
          // Handle case where _last_status_publish is 0 (first publish attempt)
          if (_last_status_publish == 0) {
            // First publish attempt - allow it immediately
            should_publish = true;
          } else {
            // Calculate elapsed time since last successful publish
            unsigned long elapsed = (now >= _last_status_publish) ? 
                                   (now - _last_status_publish) : 
                                   (ULONG_MAX - _last_status_publish + now + 1);
            should_publish = (elapsed >= _status_interval);
          }
        }
        
        if (should_publish) {
          // Only log elapsed time if we have a previous successful publish
          if (_last_status_publish != 0) {
            unsigned long elapsed = (now >= _last_status_publish) ? 
                                   (now - _last_status_publish) : 
                                   (ULONG_MAX - _last_status_publish + now + 1);
            MQTT_DEBUG_PRINTLN("Status publish timer expired (elapsed: %lu ms, interval: %lu ms)", elapsed, _status_interval);
          } else {
            MQTT_DEBUG_PRINTLN("Status publish attempt (first publish or retry)");
          }
          
          _last_status_retry = now;
          if (publishStatus()) {
            _last_status_publish = now;
            _last_status_retry = 0;
            MQTT_DEBUG_PRINTLN("Status published successfully, next publish in %lu ms", _status_interval);
          } else {
            MQTT_DEBUG_PRINTLN("Status publish failed, will retry in %lu ms", STATUS_RETRY_INTERVAL);
            // _last_status_retry already set above - will prevent immediate retry
          }
        }
      }
    }
    
    // Critical memory check (every 15 minutes) - only log warnings
    static unsigned long last_critical_check = 0;
    if (now - last_critical_check > 900000) {
      size_t max_alloc = ESP.getMaxAllocHeap();
      if (max_alloc < 40000) {
        MQTT_DEBUG_PRINTLN("CRITICAL: Low memory! Free: %d, Max: %d", ESP.getFreeHeap(), max_alloc);
      } else if (max_alloc < 60000) {
        MQTT_DEBUG_PRINTLN("WARNING: Memory pressure. Free: %d, Max: %d", ESP.getFreeHeap(), max_alloc);
      }
      last_critical_check = now;
    }
    
    // Update cached analyzer server status periodically (every 5 seconds)
    // This ensures cache stays accurate even if callbacks miss updates
    static unsigned long last_analyzer_status_update = 0;
    if (now - last_analyzer_status_update > 5000) {
      _cached_has_analyzer_servers = (_analyzer_us_enabled && _analyzer_us_client && _analyzer_us_client->connected()) ||
                                     (_analyzer_eu_enabled && _analyzer_eu_client && _analyzer_eu_client->connected());
      last_analyzer_status_update = now;
    }
    
    // Adaptive task delay based on work done
    // Check if we have work to do (queue has packets or status needs publishing)
    bool has_work = (_queue_count > 0);
    if (!has_work && _status_enabled) {
      // Check if status publish is needed soon
      if (_last_status_publish == 0 || 
          (now - _last_status_publish >= (_status_interval - 10000))) {  // Within 10s of next publish
        has_work = true;
      }
    }
    
    // Adaptive delay: shorter when work pending, longer when idle
    if (has_work) {
      vTaskDelay(pdMS_TO_TICKS(5));   // 5ms delay when work pending - process faster
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));  // 50ms delay when idle - save CPU
    }
  }
}
#endif

bool MQTTBridge::isConfigValid() const {
  return _config_valid;
}

bool MQTTBridge::isConfigValid(const NodePrefs* prefs) {
  // Check if MQTT server is configured (not default placeholder)
  if (strlen(prefs->mqtt_server) == 0 || 
      strcmp(prefs->mqtt_server, "your-mqtt-broker.com") == 0) {
    return false;
  }
  
  // Check if MQTT port is valid
  if (prefs->mqtt_port == 0 || prefs->mqtt_port > 65535) {
    return false;
  }
  
  // Username and password are optional - anonymous mode is supported
  // Only reject if they contain the default placeholder values
  if (strcmp(prefs->mqtt_username, "your-username") == 0) {
    return false;
  }
  
  if (strcmp(prefs->mqtt_password, "your-password") == 0) {
    return false;
  }
  
  return true;
}

void MQTTBridge::checkConfigurationMismatch() {
  // Check if bridge.source is set to tx (logTx) but mqtt.tx is disabled
  // This would prevent packet publishing since sendPacket() requires both packets_enabled and tx_enabled
  if (_prefs->bridge_pkt_src == 0 && _packets_enabled && !_tx_enabled) {
    unsigned long now = millis();
    // Always log on first detection, then throttle to every 5 minutes to avoid spam
    if (_last_config_warning == 0 || (now - _last_config_warning > CONFIG_WARNING_INTERVAL)) {
      MQTT_DEBUG_PRINTLN("MQTT: Configuration mismatch detected! bridge.source=tx (logTx) but mqtt.tx=off. Packets will not be published. Run 'set bridge.source rx' or 'set mqtt.tx on' to fix.");
      _last_config_warning = now;
    }
  } else {
    // Configuration is correct, reset warning timer so we log immediately if it becomes wrong again
    _last_config_warning = 0;
  }
}

bool MQTTBridge::isReady() const {
  return _initialized && isWiFiConfigValid(_prefs);
}

void MQTTBridge::loop() {
  if (!_initialized) return;
  
  #ifdef ESP_PLATFORM
  // On ESP32, loop() is a no-op - all processing happens in the FreeRTOS task
  // This method is kept for API compatibility but does nothing
  return;
  #else
  // Non-ESP32: Original loop implementation
  // Actively monitor and manage WiFi connection
  static unsigned long last_wifi_check = 0;
  static unsigned long last_wifi_reconnect_attempt = 0;
  static wl_status_t last_wifi_status = WL_DISCONNECTED;
  static bool wifi_status_initialized = false;
  static unsigned long wifi_disconnected_time = 0;
  
  unsigned long now = millis();
  wl_status_t current_wifi_status = WiFi.status();
  
  // Initialize last_wifi_status on first loop() call
  if (!wifi_status_initialized) {
    last_wifi_status = current_wifi_status;
    wifi_status_initialized = true;
    if (current_wifi_status == WL_CONNECTED && !_ntp_synced) {
      syncTimeWithNTP();
    }
  }
  
  // Check WiFi status every 10 seconds for faster detection
  if (now - last_wifi_check > 10000) {
    last_wifi_check = now;
    
    if (current_wifi_status == WL_CONNECTED) {
      if (last_wifi_status != WL_CONNECTED) {
        wifi_disconnected_time = 0;
        if (!_ntp_synced) {
          syncTimeWithNTP();
        }
      }
      last_wifi_status = WL_CONNECTED;
    } else {
      if (last_wifi_status == WL_CONNECTED) {
        wifi_disconnected_time = now;
      } else if (wifi_disconnected_time > 0) {
        unsigned long disconnected_duration = now - wifi_disconnected_time;
        
        // Try to force reconnection if disconnected for more than 30 seconds
        if (disconnected_duration > 30000 && (now - last_wifi_reconnect_attempt) > 30000) {
          last_wifi_reconnect_attempt = now;
          WiFi.disconnect();
          WiFi.begin(_prefs->wifi_ssid, _prefs->wifi_password);
        }
      }
      last_wifi_status = current_wifi_status;
    }
  }
  
  // Check for pending NTP sync (triggered from WiFi event handler)
  if (_ntp_sync_pending && WiFi.status() == WL_CONNECTED) {
    _ntp_sync_pending = false;
    syncTimeWithNTP();
  }
  
  // Check if analyzer server settings have changed in preferences
  static unsigned long last_analyzer_check = 0;
  if (millis() - last_analyzer_check > 5000) {
    last_analyzer_check = millis();
    if (_analyzer_us_enabled != _prefs->mqtt_analyzer_us_enabled || 
        _analyzer_eu_enabled != _prefs->mqtt_analyzer_eu_enabled) {
      MQTT_DEBUG_PRINTLN("Analyzer settings changed - updating...");
      setupAnalyzerServers();
    }
  }
  
  // Maintain broker connections
  connectToBrokers();
  
  // Maintain analyzer server connections
  maintainAnalyzerConnections();
  
  // Process packet queue
  processPacketQueue();
  
  // Periodic configuration check (throttled to avoid spam)
  checkConfigurationMismatch();
  
  // Periodic NTP sync (every hour) - only when connected
  if (WiFi.status() == WL_CONNECTED && millis() - _last_ntp_sync > 3600000) {
    syncTimeWithNTP();
  }
  
  // Publish status updates (handle millis() overflow correctly)
  if (_status_enabled) {
    // Use cached destination status (updated in connection callbacks) - early exit if no destinations
    bool has_custom_brokers = _cached_has_brokers && _config_valid;
    bool has_destinations = has_custom_brokers || _cached_has_analyzer_servers;
    
    // Only attempt to publish if we have destinations available
    if (has_destinations) {
      unsigned long now = millis();
      bool should_publish = false;
      
      // First, check if we need to respect retry interval (prevents spam when publish keeps failing)
      if (_last_status_retry != 0) {
        unsigned long retry_elapsed = (now >= _last_status_retry) ?
                                     (now - _last_status_retry) :
                                     (ULONG_MAX - _last_status_retry + now + 1);
        if (retry_elapsed < STATUS_RETRY_INTERVAL) {
          // Too soon to retry - wait longer
          should_publish = false;
        } else {
          // Retry interval has passed - allow retry
          should_publish = true;
        }
      } else {
        // No pending retry - check if normal interval has passed
        // Handle case where _last_status_publish is 0 (first publish attempt)
        if (_last_status_publish == 0) {
          // First publish attempt - allow it immediately
          should_publish = true;
        } else {
          // Calculate elapsed time since last successful publish
          unsigned long elapsed = (now >= _last_status_publish) ? 
                               (now - _last_status_publish) : 
                               (ULONG_MAX - _last_status_publish + now + 1);
          should_publish = (elapsed >= _status_interval);
        }
      }
      
      if (should_publish) {
        // Only log elapsed time if we have a previous successful publish
        if (_last_status_publish != 0) {
          unsigned long elapsed = (now >= _last_status_publish) ? 
                                 (now - _last_status_publish) : 
                                 (ULONG_MAX - _last_status_publish + now + 1);
          MQTT_DEBUG_PRINTLN("Status publish timer expired (elapsed: %lu ms, interval: %lu ms)", elapsed, _status_interval);
        } else {
          MQTT_DEBUG_PRINTLN("Status publish attempt (first publish or retry)");
        }
        
        _last_status_retry = now;
        if (publishStatus()) {
          _last_status_publish = now;
          _last_status_retry = 0;
          MQTT_DEBUG_PRINTLN("Status published successfully, next publish in %lu ms", _status_interval);
        } else {
          MQTT_DEBUG_PRINTLN("Status publish failed, will retry in %lu ms", STATUS_RETRY_INTERVAL);
          // _last_status_retry already set above - will prevent immediate retry
        }
      }
    } else {
      if (_last_status_retry != 0) {
        _last_status_retry = 0;
      }
    }
    
    // Check if status hasn't been published successfully for too long
    // If status publishes have been failing for > 10 minutes, force full MQTT reinitialization
    if (_status_enabled && _last_status_publish != 0) {
      unsigned long time_since_last_success = (now >= _last_status_publish) ?
                                              (now - _last_status_publish) :
                                              (ULONG_MAX - _last_status_publish + now + 1);
      const unsigned long MAX_FAILURE_TIME_MS = 600000;  // 10 minutes
      
      if (time_since_last_success > MAX_FAILURE_TIME_MS) {
        static unsigned long last_reinit_log = 0;
        if (now - last_reinit_log > 300000) {  // Log every 5 minutes max
          MQTT_DEBUG_PRINTLN("CRITICAL: Status publish has been failing for %lu ms (>%lu ms), forcing MQTT session reinitialization",
                             time_since_last_success, MAX_FAILURE_TIME_MS);
          last_reinit_log = now;
        }
        
        // Force full MQTT session reinitialization
        // Disconnect all MQTT clients
        if (_mqtt_client && _mqtt_client->connected()) {
          _mqtt_client->disconnect();
          #ifdef ESP_PLATFORM
          vTaskDelay(pdMS_TO_TICKS(100));  // Brief delay to allow disconnect
          #else
          delay(100);  // Brief delay to allow disconnect
          #endif
        }
        
        // Disconnect analyzer clients
        if (_analyzer_us_client && _analyzer_us_client->connected()) {
          _analyzer_us_client->disconnect();
        }
        if (_analyzer_eu_client && _analyzer_eu_client->connected()) {
          _analyzer_eu_client->disconnect();
        }
        
        // Reset all broker connection states
        for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
          if (_brokers[i].enabled) {
            _brokers[i].connected = false;
            _brokers[i].last_attempt = 0;  // Allow immediate reconnect
          }
        }
        _active_brokers = 0;
        _cached_has_brokers = false;
        _cached_has_analyzer_servers = false;
        
        // Reset status publish timestamp to allow fresh attempt after reconnection
        _last_status_publish = 0;
        _last_status_retry = 0;
        
        MQTT_DEBUG_PRINTLN("MQTT session reinitialized - reconnection will be attempted on next loop");
      }
    }
  }
  #endif
  
  #ifdef ESP_PLATFORM
  // Critical memory check (every 15 minutes) - only log warnings
  static unsigned long last_critical_check = 0;
  if (millis() - last_critical_check > 900000) {
    size_t max_alloc = ESP.getMaxAllocHeap();
    if (max_alloc < 40000) {
      MQTT_DEBUG_PRINTLN("CRITICAL: Low memory! Free: %d, Max: %d", ESP.getFreeHeap(), max_alloc);
    } else if (max_alloc < 60000) {
      MQTT_DEBUG_PRINTLN("WARNING: Memory pressure. Free: %d, Max: %d", ESP.getFreeHeap(), max_alloc);
    }
    last_critical_check = millis();
  }
  #endif
}

void MQTTBridge::onPacketReceived(mesh::Packet *packet) {
  if (!_initialized || !_packets_enabled) return;
  
  // Check if we have any valid brokers to send to
  bool has_valid_brokers = _config_valid || 
                          (_analyzer_us_enabled && _analyzer_us_client) ||
                          (_analyzer_eu_enabled && _analyzer_eu_client);
  
  if (!has_valid_brokers) return;
  
  // Queue packet for transmission
  queuePacket(packet, false);
}

void MQTTBridge::sendPacket(mesh::Packet *packet) {
  if (!_initialized || !_packets_enabled || !_tx_enabled) return;
  
  // Queue packet for transmission (only if TX enabled)
  queuePacket(packet, true);
}

bool MQTTBridge::isMQTTConfigValid() {
  // Check if MQTT server is configured (not default placeholder)
  if (strlen(_prefs->mqtt_server) == 0 || 
      strcmp(_prefs->mqtt_server, "your-mqtt-broker.com") == 0) {
    return false;
  }
  
  // Check if MQTT port is valid
  if (_prefs->mqtt_port == 0 || _prefs->mqtt_port > 65535) {
    return false;
  }
  
  // Username and password are optional - anonymous mode is supported
  // Only reject if they contain the default placeholder values
  if (strcmp(_prefs->mqtt_username, "your-username") == 0) {
    return false;
  }
  
  if (strcmp(_prefs->mqtt_password, "your-password") == 0) {
    return false;
  }
  
  return true;
}

bool MQTTBridge::isIATAValid() const {
  // Check if IATA code is configured (not empty, not default "XXX")
  if (strlen(_iata) == 0 || strcmp(_iata, "XXX") == 0) {
    return false;
  }
  return true;
}

void MQTTBridge::connectToBrokers() {
  // Check if MQTT configuration is valid before attempting connection
  if (!_config_valid) {
    return;
  }
  
  // Check WiFi status first - don't attempt MQTT connection if WiFi is disconnected
  if (WiFi.status() != WL_CONNECTED) {
    // WiFi is not connected - skip MQTT connection attempts
    // WiFi auto-reconnect will handle WiFi, then we can connect MQTT
    static unsigned long last_wifi_warning = 0;
    unsigned long now = millis();
    if (now - last_wifi_warning > 300000) { // Log every 5 minutes max
      MQTT_DEBUG_PRINTLN("Skipping MQTT broker connection - WiFi not connected");
      last_wifi_warning = now;
    }
    return;
  }
  
  // For now, connect to the first enabled broker
  // TODO: Implement multi-broker support with PsychicMqttClient
  for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
    if (!_brokers[i].enabled) continue;
    
    // Check if we need to attempt connection
    // Allow immediate reconnect if last_attempt is 0 (was reset due to failure)
    bool can_attempt = (_brokers[i].last_attempt == 0) || 
                       (millis() - _brokers[i].last_attempt > _brokers[i].reconnect_interval);
    
    if (!_brokers[i].connected && can_attempt) {
      MQTT_DEBUG_PRINTLN("Connecting to broker %d: %s:%d", i, _brokers[i].host, _brokers[i].port);
      
      // Generate unique client ID
      char client_id[32];
      snprintf(client_id, sizeof(client_id), "%s_%d_%lu", _origin, i, millis());
      
      // Set broker URI and connect using PsychicMqttClient API
      char broker_uri[128];
      snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", _brokers[i].host, _brokers[i].port);
      _mqtt_client->setServer(broker_uri);
      
      // Set credentials if provided
      if (strlen(_brokers[i].username) > 0) {
        _mqtt_client->setCredentials(_brokers[i].username, _brokers[i].password);
      }
      
      // Ensure we're disconnected before attempting new connection
      if (_mqtt_client->connected()) {
        _mqtt_client->disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));  // Brief delay to allow disconnect to complete
      }
      
      // Connect to the broker (PsychicMqttClient uses async connection)
      _mqtt_client->connect();
      
      // Update attempt timestamp
      _brokers[i].last_attempt = millis();
      MQTT_DEBUG_PRINTLN("Initiating connection to broker %d", i);
    }
    
    // Maintain connection and check for stale connections
    if (_brokers[i].connected) {
      // Check actual connection state - if it's stale, mark as disconnected and trigger reconnect
      // PsychicMqttClient handles automatic reconnection internally, but we need to detect stale state
      if (!_mqtt_client->connected()) {
        MQTT_DEBUG_PRINTLN("Broker %d connection lost, marking for reconnect", i);
        _brokers[i].connected = false;
        _active_brokers--;
        _brokers[i].last_attempt = 0;  // Reset attempt time to allow immediate reconnect
        // Update cached broker status
        _cached_has_brokers = isAnyBrokerConnected();
      }
      // Removed aggressive 4-hour health check that was causing connection instability.
      // The MQTT client library handles connection health internally, and forcing
      // disconnections on healthy connections was causing hours of downtime.
    } else {
      // Not connected - ensure we attempt reconnection if enough time has passed
      // Reset last_attempt if it's been too long (prevents getting stuck)
      if (_brokers[i].last_attempt > 0 && (millis() - _brokers[i].last_attempt) > 300000) {
        // Been trying for more than 5 minutes - reset to allow fresh attempt
        _brokers[i].last_attempt = 0;
      }
    }
  }
  
  // Update cached broker status after connection attempts
  _cached_has_brokers = isAnyBrokerConnected();
}

void MQTTBridge::processPacketQueue() {
  #ifdef ESP_PLATFORM
  // Use FreeRTOS queue
  if (_packet_queue_handle == nullptr) {
    return;
  }
  
  // Update queue count from actual queue state
  _queue_count = uxQueueMessagesWaiting(_packet_queue_handle);
  
  if (_queue_count == 0) {
    return;
  }
  
  // Use cached broker connection status to avoid redundant checks
  bool has_connected_brokers = _cached_has_brokers || _cached_has_analyzer_servers;
  
  if (!has_connected_brokers) {
    if (_queue_count > 0) {
      unsigned long now = millis();
      if (now - _last_no_broker_log > NO_BROKER_LOG_INTERVAL) {
        MQTT_DEBUG_PRINTLN("Queue has %d packets but no brokers connected", _queue_count);
        _last_no_broker_log = now;
      }
    }
    return;
  }
  
  _last_no_broker_log = 0;
  
  // Process up to 1 packet per call to maintain responsiveness
  int processed = 0;
  int max_per_loop = 1;
  unsigned long loop_start_time = millis();
  const unsigned long MAX_PROCESSING_TIME_MS = 30;
  
  while (processed < max_per_loop) {
    unsigned long elapsed = millis() - loop_start_time;
    if (elapsed > MAX_PROCESSING_TIME_MS) {
      break;
    }
    
    QueuedPacket queued;
    // Try to receive from queue (non-blocking)
    if (xQueueReceive(_packet_queue_handle, &queued, 0) != pdTRUE) {
      break;  // No more packets
    }
    
    // Publish packet (use stored raw data if available)
    publishPacket(queued.packet, queued.is_tx, 
                  queued.has_raw_data ? queued.raw_data : nullptr,
                  queued.has_raw_data ? queued.raw_len : 0,
                  queued.has_raw_data ? queued.snr : 0.0f,
                  queued.has_raw_data ? queued.rssi : 0.0f);
    
    // Publish raw if enabled
    if (_raw_enabled) {
      publishRaw(queued.packet);
    }
    
    // Free packet memory
    // NOTE: PacketManager::free() is not thread-safe, but in practice this should be safe because:
    // - Packets are allocated on Core 1 (main loop) and queued immediately
    // - Once queued, packets are no longer accessed by Core 1
    // - Packets are only freed here on Core 0 (MQTT task)
    // - There's no concurrent access to the same packet instance
    // However, concurrent access to PacketManager's internal pool structures could theoretically
    // cause issues. If problems occur, consider adding a mutex wrapper around PacketManager operations.
    if (queued.packet) {
      _mgr->free(queued.packet);
      queued.packet = nullptr;
    }
    
    _queue_count--;
    processed++;
    
    // No need for vTaskDelay here - task already yields at end of main loop
  }
  #else
  // Non-ESP32: Use circular buffer
  if (_queue_count == 0) {
    return;
  }
  
  // Use cached broker connection status to avoid redundant checks
  bool has_connected_brokers = _cached_has_brokers || _cached_has_analyzer_servers;
  
  if (!has_connected_brokers) {
    if (_queue_count > 0) {
      unsigned long now = millis();
      if (now - _last_no_broker_log > NO_BROKER_LOG_INTERVAL) {
        MQTT_DEBUG_PRINTLN("Queue has %d packets but no brokers connected", _queue_count);
        _last_no_broker_log = now;
      }
    }
    return;
  }
  
  _last_no_broker_log = 0;
  
  int processed = 0;
  int max_per_loop = 1;
  unsigned long loop_start_time = millis();
  const unsigned long MAX_PROCESSING_TIME_MS = 30;
  
  while (_queue_count > 0 && processed < max_per_loop) {
    unsigned long elapsed = millis() - loop_start_time;
    if (elapsed > MAX_PROCESSING_TIME_MS) {
      break;
    }
    
    QueuedPacket& queued = _packet_queue[_queue_head];
    
    publishPacket(queued.packet, queued.is_tx, 
                  queued.has_raw_data ? queued.raw_data : nullptr,
                  queued.has_raw_data ? queued.raw_len : 0,
                  queued.has_raw_data ? queued.snr : 0.0f,
                  queued.has_raw_data ? queued.rssi : 0.0f);
    
    if (_raw_enabled) {
      publishRaw(queued.packet);
    }
    
    if (queued.packet) {
      _mgr->free(queued.packet);
      queued.packet = nullptr;
    }
    
    dequeuePacket();
    processed++;
  }
  #endif
}

bool MQTTBridge::publishStatus() {
  // Check if IATA is configured before attempting to publish
  if (!isIATAValid()) {
    static unsigned long last_iata_warning = 0;
    unsigned long now = millis();
    // Only log this warning every 5 minutes to avoid spam
    if (now - last_iata_warning > 300000) {
      MQTT_DEBUG_PRINTLN("MQTT: Cannot publish status - IATA code not configured (current: '%s'). Please set mqtt.iata via CLI.", _iata);
      last_iata_warning = now;
    }
    return false;
  }
  
  // Memory pressure check: Use same threshold as packet publishes for consistency
  // Status publishes should not be skipped more aggressively than packets
  #ifdef ESP32
  unsigned long now = millis();
  if (now - _last_memory_check > 5000) {  // Check every 5 seconds
    size_t max_alloc = ESP.getMaxAllocHeap();
    if (max_alloc < 60000) {  // Less than 60KB max alloc = severe fragmentation (same as packets)
      static unsigned long last_status_skip_log = 0;
      if (now - last_status_skip_log > 300000) {  // Log every 5 minutes
        MQTT_DEBUG_PRINTLN("MQTT: Skipping status publish due to memory pressure (Max alloc: %d)", max_alloc);
        last_status_skip_log = now;
      }
      return false;  // Skip status publish
    }
    _last_memory_check = now;
  }
  #endif
  
  // Use cached destination status to avoid redundant checks
  // Note: Connection state is verified in connectToBrokers() which runs before publishStatus()
  bool has_custom_brokers = _cached_has_brokers && _config_valid;
  bool has_destinations = has_custom_brokers || _cached_has_analyzer_servers;
  
  if (!has_destinations) {
    return false;  // No destinations available
  }
  
  // Don't do aggressive pre-check like before - if packets are publishing successfully,
  // the connection is likely fine. The actual publish attempt will handle connection issues.
  
  // Status messages with stats can be larger (~400-500 bytes), so increase buffer size
  char json_buffer[768];  // Increased from 512 to accommodate stats object
  char origin_id[65];
  char timestamp[32];
  char radio_info[64];
  
  // Get current timestamp in ISO 8601 format
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000000", &timeinfo);
  } else {
    strcpy(timestamp, "2024-01-01T12:00:00.000000");
  }
  
  // Build radio info string (freq,bw,sf,cr)
  snprintf(radio_info, sizeof(radio_info), "%.6f,%.1f,%d,%d", 
           _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
  
  // Use actual device ID
  strncpy(origin_id, _device_id, sizeof(origin_id) - 1);
  origin_id[sizeof(origin_id) - 1] = '\0';
  
  // Build client version string
  char client_version[64];
  getClientVersion(client_version, sizeof(client_version));
  
  // Collect stats on-demand if sources are available
  int battery_mv = -1;
  int uptime_secs = -1;
  int errors = -1;
  int noise_floor = -999;
  int tx_air_secs = -1;
  int rx_air_secs = -1;
  
  if (_board) {
    battery_mv = _board->getBattMilliVolts();
  }
  if (_ms) {
    uptime_secs = _ms->getMillis() / 1000;
  }
  if (_dispatcher) {
    errors = _dispatcher->getErrFlags();
    tx_air_secs = _dispatcher->getTotalAirTime() / 1000;
    rx_air_secs = _dispatcher->getReceiveAirTime() / 1000;
  }
  if (_radio) {
    noise_floor = (int16_t)_radio->getNoiseFloor();
  }
  
  // Build status message with stats
  int len = MQTTMessageBuilder::buildStatusMessage(
    _origin,
    origin_id,
    _board_model,  // model - now dynamic!
    _firmware_version,  // firmware version
    radio_info,
    client_version,  // client version
    "online",
    timestamp,
    json_buffer,
    sizeof(json_buffer),
    battery_mv,
    uptime_secs,
    errors,
    _queue_count,  // Use current queue length
    noise_floor,
    tx_air_secs,
    rx_air_secs
  );
  
          if (len > 0) {
            bool published = false;
            
            // Build topic string once and reuse (optimization: avoid redundant snprintf calls)
            char topic[128];
            snprintf(topic, sizeof(topic), "meshcore/%s/%s/status", _iata, _device_id);
            size_t json_len = strlen(json_buffer); // Cache length to avoid multiple strlen() calls
            
            // Publish to all connected custom brokers
            // Use same logic as packet publishes for consistency
            if (_config_valid && _mqtt_client) {
              // Share the same broker URI tracking as packet publishes to avoid sync issues
              // Track last broker URI to avoid calling setServer() unnecessarily (memory optimization)
              // setServer() may allocate memory, so we only call it when the broker changes
              static char last_broker_uri_shared[128] = "";
              
              for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
                // Verify broker is actually connected (state might be stale)
                if (_brokers[i].enabled && _brokers[i].connected) {
                  // Check connection state right before publish (like packet publishes do)
                  if (!_mqtt_client->connected()) {
                    // Connection lost - mark as disconnected but don't disconnect here
                    // (packet publishes handle this more gracefully)
                    _brokers[i].connected = false;
                    _active_brokers--;
                    _brokers[i].last_attempt = 0;
                    _cached_has_brokers = isAnyBrokerConnected();
                    continue;
                  }
                  
                  // Build broker URI
                  char broker_uri[128];
                  snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", _brokers[i].host, _brokers[i].port);
                  
                  // Only call setServer() if broker URI changed (reduces memory allocations)
                  if (strcmp(broker_uri, last_broker_uri_shared) != 0) {
                    _mqtt_client->setServer(broker_uri);
                    strncpy(last_broker_uri_shared, broker_uri, sizeof(last_broker_uri_shared) - 1);
                    last_broker_uri_shared[sizeof(last_broker_uri_shared) - 1] = '\0';
                  }
                  
                  // Publish with timeout check - don't block if connection is slow
                  int publish_result = _mqtt_client->publish(topic, 1, true, json_buffer, json_len);
                  if (publish_result > 0) {
                    published = true;
                  } else {
                    // Publish failed - connection may be stale, force disconnect and mark for reconnect
                    static unsigned long last_status_publish_fail_log = 0;
                    unsigned long now = millis();
                    if (now - last_status_publish_fail_log > 60000) { // Log every minute max
                      MQTT_DEBUG_PRINTLN("Status publish failed (result=%d), forcing broker %d reconnect", publish_result, i);
                      last_status_publish_fail_log = now;
                    }
                    // Force disconnect to trigger reconnection
                    if (_mqtt_client->connected()) {
                      _mqtt_client->disconnect();
                    }
                    _brokers[i].connected = false;
                    _active_brokers--;
                    _brokers[i].last_attempt = 0;  // Reset attempt time to allow immediate reconnect
                    // Update cached broker status
                    _cached_has_brokers = isAnyBrokerConnected();
                  }
                }
              }
            } else if (_config_valid) {
              // Connection state is out of sync - mark all brokers as disconnected
              // (Same logic as packet publishes)
              for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
                if (_brokers[i].enabled && _brokers[i].connected) {
                  _brokers[i].connected = false;
                  _active_brokers--;
                }
              }
              _cached_has_brokers = false;
            }
            
            // Always publish to Let's Mesh Analyzer servers if enabled and connected
            // Use shared helper function to publish same JSON to both servers (avoids duplication)
            // Use same memory threshold as main check (60000) for consistency
            if (_cached_has_analyzer_servers) {
              #ifdef ESP32
              size_t max_alloc = ESP.getMaxAllocHeap();
              if (max_alloc >= 60000) {  // Same threshold as main memory check
              #endif
                // publishToAnalyzerServers returns true if at least one publish succeeded
                if (publishToAnalyzerServers(topic, json_buffer, true)) {  // retained=true for status
                  published = true;
                }
              #ifdef ESP32
              }
              #endif
            }
            
            // Return true if we successfully published to at least one destination
            if (published) {
              MQTT_DEBUG_PRINTLN("Status published");
              return true;
            }
          }
          
          return false;  // Failed to build or publish message
}

void MQTTBridge::publishPacket(mesh::Packet* packet, bool is_tx, 
                                const uint8_t* raw_data, int raw_len, 
                                float snr, float rssi) {
  if (!packet) return;
  
  // Check if IATA is configured before attempting to publish
  if (!isIATAValid()) {
    static unsigned long last_iata_warning = 0;
    unsigned long now = millis();
    // Only log this warning every 5 minutes to avoid spam
    if (now - last_iata_warning > 300000) {
      MQTT_DEBUG_PRINTLN("MQTT: Cannot publish packet - IATA code not configured (current: '%s'). Please set mqtt.iata via CLI.", _iata);
      last_iata_warning = now;
    }
    return;
  }
  
  // Memory pressure check: Skip publishes when heap is severely fragmented
  // This prevents further fragmentation and allows memory to recover
  // Threshold: Max alloc < 60KB indicates severe fragmentation
  #ifdef ESP32
  unsigned long now = millis();
  if (now - _last_memory_check > 5000) {  // Check every 5 seconds
    size_t max_alloc = ESP.getMaxAllocHeap();
    if (max_alloc < 60000) {  // Less than 60KB max alloc = severe fragmentation
      _skipped_publishes++;
      static unsigned long last_skip_log = 0;
      if (now - last_skip_log > 60000) {  // Log every minute
        MQTT_DEBUG_PRINTLN("MQTT: Skipping publish due to memory pressure (Max alloc: %d, skipped: %d)", max_alloc, _skipped_publishes);
        last_skip_log = now;
      }
      return;  // Skip this publish to allow memory to recover
    }
    _last_memory_check = now;
  }
  #endif
  
  // Size-adaptive buffer: estimate needed size based on packet size
  // Most packets are <100 bytes (need ~400 byte JSON), large packets need ~1500 bytes
  // Optimized: Use 1024 bytes for most packets, only 2048 for very large packets (>200 bytes)
  int packet_size = packet->getRawLength();
  size_t json_buffer_size = (packet_size > 200) ? 2048 : 1024;
  // Allocate buffer based on actual needed size to save stack memory
  char json_buffer[1024]; // Default to 1024, will handle large packets separately if needed
  char json_buffer_large[2048]; // Only used for large packets
  char* active_buffer = (packet_size > 200) ? json_buffer_large : json_buffer;
  size_t active_buffer_size = (packet_size > 200) ? 2048 : 1024;
  char origin_id[65];
  
  // Use actual device ID
  strncpy(origin_id, _device_id, sizeof(origin_id) - 1);
  origin_id[sizeof(origin_id) - 1] = '\0';
  
  // Build packet message using raw radio data if provided
  // Use size-adaptive buffer size based on actual packet size
  int len;
  if (raw_data && raw_len > 0) {
    // Use provided raw radio data
    len = MQTTMessageBuilder::buildPacketJSONFromRaw(
      raw_data, raw_len, packet, is_tx, _origin, origin_id, 
      snr, rssi, _timezone, active_buffer, active_buffer_size
    );
  } else if (_last_raw_len > 0 && (millis() - _last_raw_timestamp) < 1000) {
    // Fallback to global raw radio data (within 1 second of packet)
    len = MQTTMessageBuilder::buildPacketJSONFromRaw(
      _last_raw_data, _last_raw_len, packet, is_tx, _origin, origin_id, 
      _last_snr, _last_rssi, _timezone, active_buffer, active_buffer_size
    );
  } else {
    // Fallback to reconstructed packet data
    len = MQTTMessageBuilder::buildPacketJSON(
      packet, is_tx, _origin, origin_id, _timezone, active_buffer, active_buffer_size
    );
  }
  
  if (len > 0) {
    // Build topic string once and reuse (optimization: avoid redundant snprintf calls)
    char topic[128];
    snprintf(topic, sizeof(topic), "meshcore/%s/%s/packets", _iata, _device_id);
    size_t json_len = strlen(active_buffer); // Cache length to avoid multiple strlen() calls
    
    // Publish to custom brokers (only if config is valid)
    // Double-check client is actually connected before attempting publish
    if (_config_valid && _mqtt_client && _mqtt_client->connected()) {
      // Track last broker URI to avoid calling setServer() unnecessarily (memory optimization)
      // setServer() may allocate memory, so we only call it when the broker changes
      static char last_broker_uri[128] = "";
      
      for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
        // Verify broker is actually connected (state might be stale)
        if (_brokers[i].enabled && _brokers[i].connected && _mqtt_client->connected()) {
          // Build broker URI
          char broker_uri[128];
          snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", _brokers[i].host, _brokers[i].port);
          
          // Only call setServer() if broker URI changed (reduces memory allocations)
          if (strcmp(broker_uri, last_broker_uri) != 0) {
            _mqtt_client->setServer(broker_uri);
            strncpy(last_broker_uri, broker_uri, sizeof(last_broker_uri) - 1);
            last_broker_uri[sizeof(last_broker_uri) - 1] = '\0';
          }
          
          // Publish with timeout check - don't block if connection is slow
          // This prevents blocking the main loop when MQTT broker is slow or unresponsive
          int publish_result = _mqtt_client->publish(topic, 1, false, active_buffer, json_len); // qos=1, retained=false
          if (publish_result <= 0) {
            // Publish failed - connection may be stale, force disconnect and mark for reconnect
            static unsigned long last_publish_fail_log = 0;
            unsigned long now = millis();
            if (now - last_publish_fail_log > 60000) { // Log every minute max
              MQTT_DEBUG_PRINTLN("Publish failed (result=%d), forcing broker %d reconnect", publish_result, i);
              last_publish_fail_log = now;
            }
            // Force disconnect to trigger reconnection
            if (_mqtt_client->connected()) {
              _mqtt_client->disconnect();
            }
            _brokers[i].connected = false;
            _active_brokers--;
            _brokers[i].last_attempt = 0;  // Reset attempt time to allow immediate reconnect
            // Update cached broker status
            _cached_has_brokers = isAnyBrokerConnected();
          }
        }
      }
    } else if (_config_valid) {
      // Connection state is out of sync - mark all brokers as disconnected
      for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
        if (_brokers[i].enabled && _brokers[i].connected) {
          _brokers[i].connected = false;
          _active_brokers--;
        }
      }
    }
    
    // Always publish to Let's Mesh Analyzer servers (independent of custom broker config)
    // Skip analyzer servers if memory is severely fragmented (they're less critical than custom brokers)
    #ifdef ESP32
    size_t max_alloc = ESP.getMaxAllocHeap();
    if (max_alloc >= 60000) {  // Only publish to analyzer servers if memory is OK
      publishToAnalyzerServers(topic, json_buffer, false);
    }
    #else
    publishToAnalyzerServers(topic, json_buffer, false);
    #endif
  } else {
    // Debug: log when packet message building fails
    uint8_t packet_type = packet->getPayloadType();
    if (packet_type == 4 || packet_type == 9) {  // ADVERT or TRACE
      MQTT_DEBUG_PRINTLN("Failed to build packet JSON for type=%d (len=%d), packet not published", packet_type, len);
    }
  }
}

void MQTTBridge::publishRaw(mesh::Packet* packet) {
  if (!packet) return;
  
  // Check if IATA is configured before attempting to publish
  if (!isIATAValid()) {
    static unsigned long last_iata_warning = 0;
    unsigned long now = millis();
    // Only log this warning every 5 minutes to avoid spam
    if (now - last_iata_warning > 300000) {
      MQTT_DEBUG_PRINTLN("MQTT: Cannot publish raw packet - IATA code not configured (current: '%s'). Please set mqtt.iata via CLI.", _iata);
      last_iata_warning = now;
    }
    return;
  }
  
  // Size-adaptive buffer for raw JSON: use 1024 for most packets, 2048 for large ones
  int packet_size = packet->getRawLength();
  char json_buffer[1024]; // Default to 1024, will handle large packets separately if needed
  char json_buffer_large[2048]; // Only used for large packets
  char* active_buffer = (packet_size > 200) ? json_buffer_large : json_buffer;
  size_t active_buffer_size = (packet_size > 200) ? 2048 : 1024;
  char origin_id[65];
  
  // Use actual device ID
  strncpy(origin_id, _device_id, sizeof(origin_id) - 1);
  origin_id[sizeof(origin_id) - 1] = '\0';
  
  // Build raw message
  int len = MQTTMessageBuilder::buildRawJSON(
    packet, _origin, origin_id, _timezone, active_buffer, active_buffer_size
  );
  
  if (len > 0) {
    // Build topic string once and reuse (optimization: avoid redundant snprintf calls)
    char topic[128];
    snprintf(topic, sizeof(topic), "meshcore/%s/%s/raw", _iata, _device_id);
    size_t json_len = strlen(active_buffer); // Cache length to avoid multiple strlen() calls
    
    // Publish to custom brokers (only if config is valid)
    // Double-check client is actually connected before attempting publish
    if (_config_valid && _mqtt_client && _mqtt_client->connected()) {
      // Track last broker URI to avoid calling setServer() unnecessarily (memory optimization)
      // setServer() may allocate memory, so we only call it when the broker changes
      static char last_broker_uri_raw[128] = "";
      
      for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
        // Verify broker is actually connected (state might be stale)
        if (_brokers[i].enabled && _brokers[i].connected && _mqtt_client->connected()) {
          // Build broker URI
          char broker_uri[128];
          snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", _brokers[i].host, _brokers[i].port);
          
          // Only call setServer() if broker URI changed (reduces memory allocations)
          if (strcmp(broker_uri, last_broker_uri_raw) != 0) {
            _mqtt_client->setServer(broker_uri);
            strncpy(last_broker_uri_raw, broker_uri, sizeof(last_broker_uri_raw) - 1);
            last_broker_uri_raw[sizeof(last_broker_uri_raw) - 1] = '\0';
          }
          
          // Publish with timeout check - don't block if connection is slow
          int publish_result = _mqtt_client->publish(topic, 1, false, active_buffer, json_len); // qos=1, retained=false
          if (publish_result <= 0) {
            // Publish failed - connection may be stale, force disconnect and mark for reconnect
            static unsigned long last_raw_publish_fail_log = 0;
            unsigned long now = millis();
            if (now - last_raw_publish_fail_log > 60000) { // Log every minute max
              MQTT_DEBUG_PRINTLN("Raw publish failed (result=%d), forcing broker %d reconnect", publish_result, i);
              last_raw_publish_fail_log = now;
            }
            // Force disconnect to trigger reconnection
            if (_mqtt_client->connected()) {
              _mqtt_client->disconnect();
            }
            _brokers[i].connected = false;
            _active_brokers--;
            _brokers[i].last_attempt = 0;  // Reset attempt time to allow immediate reconnect
            // Update cached broker status
            _cached_has_brokers = isAnyBrokerConnected();
          }
        }
      }
    }
    
    // Always publish to Let's Mesh Analyzer servers (independent of custom broker config)
    // Skip analyzer servers if memory is severely fragmented (they're less critical than custom brokers)
    #ifdef ESP32
    size_t max_alloc = ESP.getMaxAllocHeap();
    if (max_alloc >= 60000) {  // Only publish to analyzer servers if memory is OK
      publishToAnalyzerServers(topic, active_buffer, false);
    }
    #else
    publishToAnalyzerServers(topic, active_buffer, false);
    #endif
  }
}

void MQTTBridge::queuePacket(mesh::Packet* packet, bool is_tx) {
  #ifdef ESP_PLATFORM
  // Use FreeRTOS queue for thread-safe operation
  if (_packet_queue_handle == nullptr) {
    return;  // Queue not initialized
  }
  
  QueuedPacket queued;
  memset(&queued, 0, sizeof(QueuedPacket));
  
  queued.packet = packet;
  queued.timestamp = millis();
  queued.is_tx = is_tx;
  queued.has_raw_data = false;
  
  // Capture raw radio data with mutex protection
  // Use non-blocking mutex to prevent Core 1 from blocking - if mutex is busy, skip raw data
  if (!is_tx) {
    if (xSemaphoreTake(_raw_data_mutex, 0) == pdTRUE) {
      unsigned long current_time = millis();
      if (_last_raw_len > 0 && (current_time - _last_raw_timestamp) < 1000) {
        if (_last_raw_len <= sizeof(queued.raw_data)) {
          memcpy(queued.raw_data, _last_raw_data, _last_raw_len);
          queued.raw_len = _last_raw_len;
          queued.snr = _last_snr;
          queued.rssi = _last_rssi;
          queued.has_raw_data = true;
        }
      }
      xSemaphoreGive(_raw_data_mutex);
    }
    // If mutex unavailable, packet is queued without raw data (acceptable trade-off for responsiveness)
  }
  
  // Try to send to queue (non-blocking)
  if (xQueueSend(_packet_queue_handle, &queued, 0) != pdTRUE) {
    // Queue full - try to remove oldest packet
    QueuedPacket oldest;
    if (xQueueReceive(_packet_queue_handle, &oldest, 0) == pdTRUE) {
      if (oldest.packet) {
        MQTT_DEBUG_PRINTLN("Queue full, dropping oldest packet");
        _mgr->free(oldest.packet);
      }
      // Now try to send again
      if (xQueueSend(_packet_queue_handle, &queued, 0) != pdTRUE) {
        MQTT_DEBUG_PRINTLN("Failed to queue packet after dropping oldest");
        return;
      }
    } else {
      MQTT_DEBUG_PRINTLN("Queue full and cannot remove oldest packet");
      return;
    }
  }
  
  // Update queue count (approximate, since we can't atomically update it)
  UBaseType_t queue_messages = uxQueueMessagesWaiting(_packet_queue_handle);
  _queue_count = queue_messages;
  #else
  // Non-ESP32: Use circular buffer
  if (_queue_count >= MAX_QUEUE_SIZE) {
    QueuedPacket& oldest = _packet_queue[_queue_head];
    if (oldest.packet) {
      MQTT_DEBUG_PRINTLN("Queue full, dropping oldest packet (queue size: %d)", _queue_count);
      _mgr->free(oldest.packet);
      oldest.packet = nullptr;
    }
    dequeuePacket();
  }
  
  QueuedPacket& queued = _packet_queue[_queue_tail];
  memset(&queued, 0, sizeof(QueuedPacket));
  
  queued.packet = packet;
  queued.timestamp = millis();
  queued.is_tx = is_tx;
  queued.has_raw_data = false;
  
  if (!is_tx && _last_raw_len > 0 && (millis() - _last_raw_timestamp) < 1000) {
    if (_last_raw_len <= sizeof(queued.raw_data)) {
      memcpy(queued.raw_data, _last_raw_data, _last_raw_len);
      queued.raw_len = _last_raw_len;
      queued.snr = _last_snr;
      queued.rssi = _last_rssi;
      queued.has_raw_data = true;
    }
  }
  
  _queue_tail = (_queue_tail + 1) % MAX_QUEUE_SIZE;
  _queue_count++;
  #endif
}

void MQTTBridge::dequeuePacket() {
  #ifdef ESP_PLATFORM
  // On ESP32, dequeuePacket() is not used - we use FreeRTOS queue operations directly
  // This method should never be called on ESP32
  return;
  #else
  // Non-ESP32: Use circular buffer
  if (_queue_count == 0) return;
  
  // Clear the dequeued packet structure to free memory and prevent stale data
  QueuedPacket& dequeued = _packet_queue[_queue_head];
  memset(&dequeued, 0, sizeof(QueuedPacket));
  dequeued.has_raw_data = false; // Explicitly set after memset
  
  _queue_head = (_queue_head + 1) % MAX_QUEUE_SIZE;
  _queue_count--;
  #endif
}

bool MQTTBridge::isAnyBrokerConnected() {
  for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
    if (_brokers[i].enabled && _brokers[i].connected) {
      return true;
    }
  }
  return false;
}

void MQTTBridge::setBrokerDefaults() {
  for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
    memset(&_brokers[i], 0, sizeof(MQTTBroker));
    _brokers[i].port = 1883;
    _brokers[i].qos = 0;
    _brokers[i].enabled = false;
    _brokers[i].connected = false;
    _brokers[i].reconnect_interval = 5000; // 5 seconds
  }
}

void MQTTBridge::setBroker(int broker_index, const char* host, uint16_t port, 
                          const char* username, const char* password, bool enabled) {
  if (broker_index < 0 || broker_index >= MAX_MQTT_BROKERS_COUNT) return;
  
  MQTTBroker& broker = _brokers[broker_index];
  strncpy(broker.host, host, sizeof(broker.host) - 1);
  broker.port = port;
  strncpy(broker.username, username, sizeof(broker.username) - 1);
  strncpy(broker.password, password, sizeof(broker.password) - 1);
  broker.enabled = enabled;
  broker.connected = false;
  broker.reconnect_interval = 5000;
}

void MQTTBridge::setOrigin(const char* origin) {
  strncpy(_origin, origin, sizeof(_origin) - 1);
  _origin[sizeof(_origin) - 1] = '\0';
}

void MQTTBridge::setIATA(const char* iata) {
  strncpy(_iata, iata, sizeof(_iata) - 1);
  _iata[sizeof(_iata) - 1] = '\0';
  // Convert IATA code to uppercase (IATA codes are conventionally uppercase)
  for (int i = 0; _iata[i]; i++) {
    _iata[i] = toupper(_iata[i]);
  }
}

void MQTTBridge::setDeviceID(const char* device_id) {
  strncpy(_device_id, device_id, sizeof(_device_id) - 1);
  _device_id[sizeof(_device_id) - 1] = '\0';
  MQTT_DEBUG_PRINTLN("Device ID set to: %s", _device_id);
}

void MQTTBridge::setFirmwareVersion(const char* firmware_version) {
  strncpy(_firmware_version, firmware_version, sizeof(_firmware_version) - 1);
  _firmware_version[sizeof(_firmware_version) - 1] = '\0';
}

void MQTTBridge::setBoardModel(const char* board_model) {
  strncpy(_board_model, board_model, sizeof(_board_model) - 1);
  _board_model[sizeof(_board_model) - 1] = '\0';
}

void MQTTBridge::setBuildDate(const char* build_date) {
  strncpy(_build_date, build_date, sizeof(_build_date) - 1);
  _build_date[sizeof(_build_date) - 1] = '\0';
}

void MQTTBridge::storeRawRadioData(const uint8_t* raw_data, int len, float snr, float rssi) {
  if (len > 0 && len <= sizeof(_last_raw_data)) {
    #ifdef ESP_PLATFORM
    // Protect with mutex for thread-safe access
    if (_raw_data_mutex != nullptr && xSemaphoreTake(_raw_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      memcpy(_last_raw_data, raw_data, len);
      _last_raw_len = len;
      _last_snr = snr;
      _last_rssi = rssi;
      _last_raw_timestamp = millis();
      xSemaphoreGive(_raw_data_mutex);
      MQTT_DEBUG_PRINTLN("Stored raw radio data: %d bytes, SNR=%.1f, RSSI=%.1f", len, snr, rssi);
    }
    #else
    memcpy(_last_raw_data, raw_data, len);
    _last_raw_len = len;
    _last_snr = snr;
    _last_rssi = rssi;
    _last_raw_timestamp = millis();
    MQTT_DEBUG_PRINTLN("Stored raw radio data: %d bytes, SNR=%.1f, RSSI=%.1f", len, snr, rssi);
    #endif
  }
}

void MQTTBridge::setupAnalyzerServers() {
  // Update analyzer server settings from preferences
  bool previous_us_enabled = _analyzer_us_enabled;
  bool previous_eu_enabled = _analyzer_eu_enabled;
  
  _analyzer_us_enabled = _prefs->mqtt_analyzer_us_enabled;
  _analyzer_eu_enabled = _prefs->mqtt_analyzer_eu_enabled;
  
  MQTT_DEBUG_PRINTLN("Analyzer servers - US: %s, EU: %s", 
                     _analyzer_us_enabled ? "enabled" : "disabled",
                     _analyzer_eu_enabled ? "enabled" : "disabled");
  
  // Create authentication token if any analyzer servers are enabled
  // Only create tokens if WiFi is connected and NTP is synced (to ensure correct timestamps)
  if (_analyzer_us_enabled || _analyzer_eu_enabled) {
    if (WiFi.status() == WL_CONNECTED && _ntp_synced) {
      if (createAuthToken()) {
        MQTT_DEBUG_PRINTLN("Created authentication token for analyzer servers");
        // Update client credentials with new tokens if clients exist
        if (_analyzer_us_enabled && _analyzer_us_client && strlen(_auth_token_us) > 0) {
          _analyzer_us_client->setCredentials(_analyzer_username, _auth_token_us);
        }
        if (_analyzer_eu_enabled && _analyzer_eu_client && strlen(_auth_token_eu) > 0) {
          _analyzer_eu_client->setCredentials(_analyzer_username, _auth_token_eu);
        }
      } else {
        MQTT_DEBUG_PRINTLN("Failed to create authentication token");
      }
    } else {
      MQTT_DEBUG_PRINTLN("Deferring JWT token creation - WiFi: %s, NTP: %s", 
                        (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected",
                        _ntp_synced ? "synced" : "not synced");
    }
  }
  
  // If settings changed and bridge is already initialized, recreate clients
  // This handles the case where settings change after initialization
  if (_initialized && (previous_us_enabled != _analyzer_us_enabled || previous_eu_enabled != _analyzer_eu_enabled)) {
    MQTT_DEBUG_PRINTLN("Analyzer server settings changed - recreating clients");
    setupAnalyzerClients();
  }
}

bool MQTTBridge::createAuthToken() {
  if (!_identity) {
    MQTT_DEBUG_PRINTLN("No identity for auth token");
    return false;
  }
  
  // Create username in the format: v1_{UPPERCASE_PUBLIC_KEY}
  char public_key_hex[65];
  mesh::Utils::toHex(public_key_hex, _identity->pub_key, PUB_KEY_SIZE);
  snprintf(_analyzer_username, sizeof(_analyzer_username), "v1_%s", public_key_hex);
  
  bool us_token_created = false;
  bool eu_token_created = false;
  
  unsigned long current_time = time(nullptr);
  unsigned long expires_in = 86400; // 24 hours
  bool time_synced = (current_time >= 1000000000);
  
  // Prepare owner public key (if set) - convert to uppercase hex
  const char* owner_key = nullptr;
  char owner_key_uppercase[65];
  if (_prefs->mqtt_owner_public_key[0] != '\0') {
    strncpy(owner_key_uppercase, _prefs->mqtt_owner_public_key, sizeof(owner_key_uppercase) - 1);
    owner_key_uppercase[sizeof(owner_key_uppercase) - 1] = '\0';
    for (int i = 0; owner_key_uppercase[i]; i++) {
      owner_key_uppercase[i] = toupper(owner_key_uppercase[i]);
    }
    owner_key = owner_key_uppercase;
  }
  
  char client_version[64];
  getClientVersion(client_version, sizeof(client_version));
  
  const char* email = (_prefs->mqtt_email[0] != '\0') ? _prefs->mqtt_email : nullptr;
  
  // Create JWT token for US server
  if (_analyzer_us_enabled) {
    if (JWTHelper::createAuthToken(
        *_identity, "mqtt-us-v1.letsmesh.net", 
        0, expires_in, _auth_token_us, sizeof(_auth_token_us),
        owner_key, client_version, email)) {
      us_token_created = true;
      _token_us_expires_at = time_synced ? (current_time + expires_in) : 0;
    } else {
      MQTT_DEBUG_PRINTLN("Failed to create US token");
      _token_us_expires_at = 0;
    }
  }
  
  // Create JWT token for EU server
  if (_analyzer_eu_enabled) {
    if (JWTHelper::createAuthToken(
        *_identity, "mqtt-eu-v1.letsmesh.net", 
        0, expires_in, _auth_token_eu, sizeof(_auth_token_eu),
        owner_key, client_version, email)) {
      eu_token_created = true;
      _token_eu_expires_at = time_synced ? (current_time + expires_in) : 0;
    } else {
      MQTT_DEBUG_PRINTLN("Failed to create EU token");
      _token_eu_expires_at = 0;
    }
  }
  
  if (us_token_created || eu_token_created) {
    MQTT_DEBUG_PRINTLN("Auth tokens created (US:%s EU:%s)", 
                       us_token_created ? "yes" : "no", eu_token_created ? "yes" : "no");
  }
  
  return us_token_created || eu_token_created;
}

bool MQTTBridge::publishToAnalyzerServers(const char* topic, const char* payload, bool retained) {
  if (!_analyzer_us_enabled && !_analyzer_eu_enabled) return false;
  
  bool published = false;
  
  // Publish to US server if enabled
  if (_analyzer_us_enabled && _analyzer_us_client) {
    if (publishToAnalyzerClient(_analyzer_us_client, topic, payload, retained)) {
      published = true;
    }
  }
  
  // Publish to EU server if enabled
  if (_analyzer_eu_enabled && _analyzer_eu_client) {
    if (publishToAnalyzerClient(_analyzer_eu_client, topic, payload, retained)) {
      published = true;
    }
  }
  
  return published;  // Return true if at least one publish succeeded
}

// Google Trust Services - GTS Root R4
const char* GTS_ROOT_R4 = 
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDejCCAmKgAwIBAgIQf+UwvzMTQ77dghYQST2KGzANBgkqhkiG9w0BAQsFADBX\n"
    "MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n"
    "CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIzMTEx\n"
    "NTAzNDMyMVoXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n"
    "GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFI0\n"
    "MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE83Rzp2iLYK5DuDXFgTB7S0md+8Fhzube\n"
    "Rr1r1WEYNa5A3XP3iZEwWus87oV8okB2O6nGuEfYKueSkWpz6bFyOZ8pn6KY019e\n"
    "WIZlD6GEZQbR3IvJx3PIjGov5cSr0R2Ko4H/MIH8MA4GA1UdDwEB/wQEAwIBhjAd\n"
    "BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDwYDVR0TAQH/BAUwAwEB/zAd\n"
    "BgNVHQ4EFgQUgEzW63T/STaj1dj8tT7FavCUHYwwHwYDVR0jBBgwFoAUYHtmGkUN\n"
    "l8qJUC99BM00qP/8/UswNgYIKwYBBQUHAQEEKjAoMCYGCCsGAQUFBzAChhpodHRw\n"
    "Oi8vaS5wa2kuZ29vZy9nc3IxLmNydDAtBgNVHR8EJjAkMCKgIKAehhxodHRwOi8v\n"
    "Yy5wa2kuZ29vZy9yL2dzcjEuY3JsMBMGA1UdIAQMMAowCAYGZ4EMAQIBMA0GCSqG\n"
    "SIb3DQEBCwUAA4IBAQAYQrsPBtYDh5bjP2OBDwmkoWhIDDkic574y04tfzHpn+cJ\n"
    "odI2D4SseesQ6bDrarZ7C30ddLibZatoKiws3UL9xnELz4ct92vID24FfVbiI1hY\n"
    "+SW6FoVHkNeWIP0GCbaM4C6uVdF5dTUsMVs/ZbzNnIdCp5Gxmx5ejvEau8otR/Cs\n"
    "kGN+hr/W5GvT1tMBjgWKZ1i4//emhA1JG1BbPzoLJQvyEotc03lXjTaCzv8mEbep\n"
    "8RqZ7a2CPsgRbuvTPBwcOMBBmuFeU88+FSBX6+7iP0il8b4Z0QFqIwwMHfs/L6K1\n"
    "vepuoxtGzi4CZ68zJpiq1UvSqTbFJjtbD4seiMHl\n"
    "-----END CERTIFICATE-----\n";

void MQTTBridge::setupAnalyzerClients() {
  MQTT_DEBUG_PRINTLN("Setting up PsychicMqttClient WebSocket clients...");
  MQTT_DEBUG_PRINTLN("Analyzer servers - US: %s, EU: %s", 
                     _analyzer_us_enabled ? "enabled" : "disabled",
                     _analyzer_eu_enabled ? "enabled" : "disabled");

  // Clean up existing clients if they're no longer enabled
  // This handles the case where settings change after initialization
  if (!_analyzer_us_enabled && _analyzer_us_client) {
    MQTT_DEBUG_PRINTLN("US analyzer disabled - cleaning up client");
    _analyzer_us_client->disconnect();
    delete _analyzer_us_client;
    _analyzer_us_client = nullptr;
  }
  
  if (!_analyzer_eu_enabled && _analyzer_eu_client) {
    MQTT_DEBUG_PRINTLN("EU analyzer disabled - cleaning up client");
    _analyzer_eu_client->disconnect();
    delete _analyzer_eu_client;
    _analyzer_eu_client = nullptr;
  }

  if (!_analyzer_us_enabled && !_analyzer_eu_enabled) {
    MQTT_DEBUG_PRINTLN("No analyzer servers enabled, skipping PsychicMqttClient setup");
    return;
  }

  // Setup US server client (only if enabled and doesn't already exist)
  if (_analyzer_us_enabled && !_analyzer_us_client) {
    _analyzer_us_client = new PsychicMqttClient();
    
    // Optimize MQTT client configuration for memory efficiency
    // Analyzer clients use 768-byte JWT tokens, need larger buffer for CONNECT message
    optimizeMqttClientConfig(_analyzer_us_client, true);

    // Set up event callbacks for US server
    _analyzer_us_client->onConnect([this](bool sessionPresent) {
      MQTT_DEBUG_PRINTLN("Connected to US analyzer");
      // Update cached analyzer server status
      _cached_has_analyzer_servers = (_analyzer_us_enabled && _analyzer_us_client && _analyzer_us_client->connected()) ||
                                     (_analyzer_eu_enabled && _analyzer_eu_client && _analyzer_eu_client->connected());
      publishStatusToAnalyzerClient(_analyzer_us_client, "mqtt-us-v1.letsmesh.net");
    });

    _analyzer_us_client->onDisconnect([this](bool sessionPresent) {
      MQTT_DEBUG_PRINTLN("Disconnected from US analyzer");
      // Update cached analyzer server status
      _cached_has_analyzer_servers = (_analyzer_us_enabled && _analyzer_us_client && _analyzer_us_client->connected()) ||
                                     (_analyzer_eu_enabled && _analyzer_eu_client && _analyzer_eu_client->connected());
    });

    _analyzer_us_client->onError([this](esp_mqtt_error_codes error) {
      MQTT_DEBUG_PRINTLN("US analyzer error: type=%d, code=%d", error.error_type, error.connect_return_code);
    });

    _analyzer_us_client->setServer("wss://mqtt-us-v1.letsmesh.net:443/mqtt");
    _analyzer_us_client->setCredentials(_analyzer_username, _auth_token_us);
    _analyzer_us_client->setCACert(GTS_ROOT_R4);

    if (WiFi.status() == WL_CONNECTED && _ntp_synced) {
      _analyzer_us_client->connect();
    }
  }

  // Setup EU server client (only if enabled and doesn't already exist)
  if (_analyzer_eu_enabled && !_analyzer_eu_client) {
    _analyzer_eu_client = new PsychicMqttClient();
    
    // Optimize MQTT client configuration for memory efficiency
    // Analyzer clients use 768-byte JWT tokens, need larger buffer for CONNECT message
    optimizeMqttClientConfig(_analyzer_eu_client, true);

    // Set up event callbacks for EU server
    _analyzer_eu_client->onConnect([this](bool sessionPresent) {
      MQTT_DEBUG_PRINTLN("Connected to EU analyzer");
      // Update cached analyzer server status
      _cached_has_analyzer_servers = (_analyzer_us_enabled && _analyzer_us_client && _analyzer_us_client->connected()) ||
                                     (_analyzer_eu_enabled && _analyzer_eu_client && _analyzer_eu_client->connected());
      publishStatusToAnalyzerClient(_analyzer_eu_client, "mqtt-eu-v1.letsmesh.net");
    });

    _analyzer_eu_client->onDisconnect([this](bool sessionPresent) {
      MQTT_DEBUG_PRINTLN("Disconnected from EU analyzer");
      // Update cached analyzer server status
      _cached_has_analyzer_servers = (_analyzer_us_enabled && _analyzer_us_client && _analyzer_us_client->connected()) ||
                                     (_analyzer_eu_enabled && _analyzer_eu_client && _analyzer_eu_client->connected());
    });

    _analyzer_eu_client->onError([this](esp_mqtt_error_codes error) {
      MQTT_DEBUG_PRINTLN("EU analyzer error: type=%d, code=%d", error.error_type, error.connect_return_code);
    });

    _analyzer_eu_client->setServer("wss://mqtt-eu-v1.letsmesh.net:443/mqtt");
    _analyzer_eu_client->setCredentials(_analyzer_username, _auth_token_eu);
    _analyzer_eu_client->setCACert(GTS_ROOT_R4);

    if (WiFi.status() == WL_CONNECTED && _ntp_synced) {
      _analyzer_eu_client->connect();
    }
  }
}

bool MQTTBridge::publishToAnalyzerClient(PsychicMqttClient* client, const char* topic, const char* payload, bool retained) {
  if (!client) {
    return false; // Don't log null client - this is expected if analyzer is disabled
  }
  
  if (!client->connected()) {
    // Throttle log spam - only log periodically for each analyzer server
    unsigned long now = millis();
    bool should_log = false;
    
    if (client == _analyzer_us_client && (now - _last_analyzer_us_log > ANALYZER_LOG_INTERVAL)) {
      should_log = true;
      _last_analyzer_us_log = now;
    } else if (client == _analyzer_eu_client && (now - _last_analyzer_eu_log > ANALYZER_LOG_INTERVAL)) {
      should_log = true;
      _last_analyzer_eu_log = now;
    }
    
    if (should_log) {
      MQTT_DEBUG_PRINTLN("PsychicMqttClient not connected - skipping publish to topic: %s", topic);
    }
    return false;
  }
  
  // Reset log timer when connected
  if (client == _analyzer_us_client) {
    _last_analyzer_us_log = 0;
  } else if (client == _analyzer_eu_client) {
    _last_analyzer_eu_log = 0;
  }
  
  int result = client->publish(topic, 1, retained, payload, strlen(payload));
  if (result <= 0) {
    static unsigned long last_analyzer_publish_fail_log = 0;
    unsigned long now = millis();
    if (now - last_analyzer_publish_fail_log > 60000) { // Log every minute max
      MQTT_DEBUG_PRINTLN("Analyzer publish failed (result=%d)", result);
      last_analyzer_publish_fail_log = now;
    }
    return false;
  }
  
  return true;  // Publish succeeded
}

void MQTTBridge::publishStatusToAnalyzerClient(PsychicMqttClient* client, const char* server_name) {
  if (!client || !client->connected()) {
    return;
  }
  
  // Check if IATA is configured before attempting to publish
  if (!isIATAValid()) {
    static unsigned long last_iata_warning = 0;
    unsigned long now = millis();
    // Only log this warning every 5 minutes to avoid spam
    if (now - last_iata_warning > 300000) {
      MQTT_DEBUG_PRINTLN("MQTT: Cannot publish status to analyzer - IATA code not configured (current: '%s'). Please set mqtt.iata via CLI.", _iata);
      last_iata_warning = now;
    }
    return;
  }
  
  // Create status message
  char status_topic[128];
  snprintf(status_topic, sizeof(status_topic), "meshcore/%s/%s/status", _iata, _device_id);
  
  // Build proper status message using MQTTMessageBuilder
  // Status messages with stats can be larger (~400-500 bytes)
  char json_buffer[768];  // Increased from 512 to accommodate stats object
  char origin_id[65];
  char timestamp[32];
  char radio_info[64];
  
  // Get current timestamp in ISO 8601 format
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000000", &timeinfo);
  } else {
    strcpy(timestamp, "2024-01-01T12:00:00.000000");
  }
  
  // Build radio info string (freq,bw,sf,cr)
  snprintf(radio_info, sizeof(radio_info), "%.6f,%.1f,%d,%d", 
           _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
  
  // Use actual device ID
  strncpy(origin_id, _device_id, sizeof(origin_id) - 1);
  origin_id[sizeof(origin_id) - 1] = '\0';
  
  // Build client version string
  char client_version[64];
  getClientVersion(client_version, sizeof(client_version));
  
  // Collect stats on-demand if sources are available
  int battery_mv = -1;
  int uptime_secs = -1;
  int errors = -1;
  int noise_floor = -999;
  int tx_air_secs = -1;
  int rx_air_secs = -1;
  
  if (_board) {
    battery_mv = _board->getBattMilliVolts();
  }
  if (_ms) {
    uptime_secs = _ms->getMillis() / 1000;
  }
  if (_dispatcher) {
    errors = _dispatcher->getErrFlags();
    tx_air_secs = _dispatcher->getTotalAirTime() / 1000;
    rx_air_secs = _dispatcher->getReceiveAirTime() / 1000;
  }
  if (_radio) {
    noise_floor = (int16_t)_radio->getNoiseFloor();
  }
  
  // Build status message using MQTTMessageBuilder with stats
  int len = MQTTMessageBuilder::buildStatusMessage(
    _origin,
    origin_id,
    _board_model,  // model
    _firmware_version,  // firmware version
    radio_info,
    client_version,  // client version
    "online",
    timestamp,
    json_buffer,
    sizeof(json_buffer),
    battery_mv,
    uptime_secs,
    errors,
    _queue_count,  // Use current queue length
    noise_floor,
    tx_air_secs,
    rx_air_secs
  );
  
  if (len > 0) {
    int result = client->publish(status_topic, 1, true, json_buffer, strlen(json_buffer));
    if (result <= 0) {
      MQTT_DEBUG_PRINTLN("Status publish to %s failed", server_name);
    }
  }
}

void MQTTBridge::maintainAnalyzerConnections() {
  if (!_identity) {
    return;
  }
  
  // Check WiFi status first - don't attempt MQTT reconnection if WiFi is disconnected
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  // Check NTP sync status - JWT tokens require valid timestamps
  if (!_ntp_synced) {
    return;
  }
  
  // Create JWT tokens if they don't exist yet and conditions are met
  if ((_analyzer_us_enabled || _analyzer_eu_enabled) && 
      (strlen(_auth_token_us) == 0 && strlen(_auth_token_eu) == 0)) {
    if (createAuthToken()) {
      if (_analyzer_us_enabled && _analyzer_us_client && strlen(_auth_token_us) > 0) {
        _analyzer_us_client->setCredentials(_analyzer_username, _auth_token_us);
        if (!_analyzer_us_client->connected()) {
          _analyzer_us_client->connect();
        }
      }
      if (_analyzer_eu_enabled && _analyzer_eu_client && strlen(_auth_token_eu) > 0) {
        _analyzer_eu_client->setCredentials(_analyzer_username, _auth_token_eu);
        if (!_analyzer_eu_client->connected()) {
          _analyzer_eu_client->connect();
        }
      }
    }
  }
  
  unsigned long current_time = time(nullptr);
  // If time is not synced (time() returns 0 or very small value), skip expiration checks
  // Tokens will still work but we can't track expiration properly
  // If expiration time was set before time sync, it will be a small value, so we'll renew
  bool time_synced = (current_time >= 1000000000); // After year 2001
  
  const unsigned long RENEWAL_BUFFER = 60; // Renew tokens 60 seconds before expiration (minimal buffer to avoid downtime)
  const unsigned long DISCONNECT_THRESHOLD = 60; // Only disconnect if token expires within 60 seconds
  const unsigned long RENEWAL_THROTTLE_MS = 60000; // Don't attempt renewal more than once per minute
  const unsigned long RECONNECT_THROTTLE_MS = 60000; // Don't attempt reconnection more than once per minute
  
  unsigned long now_millis = millis();
  
  // Check and renew US server token if needed
  if (_analyzer_us_enabled && _analyzer_us_client) {
    // Check if token is expired or will expire soon
    // Only check expiration if time is synced - if time isn't synced, we can't validate expiration
    // If time wasn't synced when token was created, expiration time will be invalid (< 1000000000), so renew when time syncs
    bool token_needs_renewal = false;
    if (!time_synced) {
      // Time not synced yet - only renew if token is missing (expires_at == 0)
      // Don't renew if token exists but expiration is invalid - wait for time sync
      token_needs_renewal = (_token_us_expires_at == 0);
    } else {
      // Time is synced - check if token needs renewal
      token_needs_renewal = (_token_us_expires_at == 0) || 
                           !(_token_us_expires_at >= 1000000000) || // Expiration time invalid (created before time sync)
                           (current_time >= _token_us_expires_at) ||
                           (current_time >= (_token_us_expires_at - RENEWAL_BUFFER));
    }
    
    // Throttle renewal attempts - don't try more than once per minute to avoid blocking
    bool can_attempt_renewal = (now_millis - _last_token_renewal_attempt_us) >= RENEWAL_THROTTLE_MS;
    
    // Check if client is disconnected and needs reconnection with new token
    bool needs_reconnect = !_analyzer_us_client->connected();
    
    if (token_needs_renewal && can_attempt_renewal) {
      _last_token_renewal_attempt_us = now_millis;
      
      // Prepare owner public key (if set) - convert to uppercase hex
      const char* owner_key = nullptr;
      char owner_key_uppercase[65];
      if (_prefs->mqtt_owner_public_key[0] != '\0') {
        // Copy and convert to uppercase
        strncpy(owner_key_uppercase, _prefs->mqtt_owner_public_key, sizeof(owner_key_uppercase) - 1);
        owner_key_uppercase[sizeof(owner_key_uppercase) - 1] = '\0';
        for (int i = 0; owner_key_uppercase[i]; i++) {
          owner_key_uppercase[i] = toupper(owner_key_uppercase[i]);
        }
        owner_key = owner_key_uppercase;
      }
      
      // Build client version string (same format as used in status messages)
      char client_version[64];
      getClientVersion(client_version, sizeof(client_version));
      
      // Get email from preferences (if set)
      const char* email = nullptr;
      if (_prefs->mqtt_email[0] != '\0') {
        email = _prefs->mqtt_email;
      }
      
      // Store old expiration time before renewing (to check if we need to disconnect)
      unsigned long old_token_expires_at = _token_us_expires_at;
      
      // Renew the token
      if (JWTHelper::createAuthToken(
          *_identity, "mqtt-us-v1.letsmesh.net", 
          0, 86400, _auth_token_us, sizeof(_auth_token_us),
          owner_key, client_version, email)) {
        unsigned long expires_in = 86400; // 24 hours
        _token_us_expires_at = time_synced ? (current_time + expires_in) : 0;
        MQTT_DEBUG_PRINTLN("US token renewed");
        
        _analyzer_us_client->setCredentials(_analyzer_username, _auth_token_us);
        
        bool old_token_expired_or_imminent = !time_synced || 
                                            (old_token_expires_at == 0) ||
                                            (current_time >= old_token_expires_at) ||
                                            (time_synced && old_token_expires_at >= 1000000000 && 
                                             current_time >= (old_token_expires_at - DISCONNECT_THRESHOLD));
        
        if (old_token_expired_or_imminent && _analyzer_us_client->connected()) {
          _analyzer_us_client->disconnect();
          _last_reconnect_attempt_us = now_millis;
          _analyzer_us_client->connect();
        } else if (!_analyzer_us_client->connected()) {
          _last_reconnect_attempt_us = now_millis;
          _analyzer_us_client->connect();
        }
      } else {
        MQTT_DEBUG_PRINTLN("Failed to renew US token");
        _token_us_expires_at = 0;
      }
    } else if (needs_reconnect) {
      unsigned long reconnect_elapsed = (now_millis >= _last_reconnect_attempt_us) ?
                                      (now_millis - _last_reconnect_attempt_us) :
                                      (ULONG_MAX - _last_reconnect_attempt_us + now_millis + 1);
      if (reconnect_elapsed >= RECONNECT_THROTTLE_MS) {
        _last_reconnect_attempt_us = now_millis;
        _analyzer_us_client->connect();
      } else {
        static unsigned long last_throttle_log_us = 0;
        if (now_millis - last_throttle_log_us > 300000) {
          last_throttle_log_us = now_millis;
        }
      }
    }
  }
  
  // Check and renew EU server token if needed
  if (_analyzer_eu_enabled && _analyzer_eu_client) {
    // Check if token is expired or will expire soon
    // Only check expiration if time is synced - if time isn't synced, we can't validate expiration
    // If time wasn't synced when token was created, expiration time will be invalid (< 1000000000), so renew when time syncs
    bool token_needs_renewal = false;
    if (!time_synced) {
      // Time not synced yet - only renew if token is missing (expires_at == 0)
      // Don't renew if token exists but expiration is invalid - wait for time sync
      token_needs_renewal = (_token_eu_expires_at == 0);
    } else {
      // Time is synced - check if token needs renewal
      token_needs_renewal = (_token_eu_expires_at == 0) || 
                           !(_token_eu_expires_at >= 1000000000) || // Expiration time invalid (created before time sync)
                           (current_time >= _token_eu_expires_at) ||
                           (current_time >= (_token_eu_expires_at - RENEWAL_BUFFER));
    }
    
    // Throttle renewal attempts - don't try more than once per minute to avoid blocking
    bool can_attempt_renewal = (now_millis - _last_token_renewal_attempt_eu) >= RENEWAL_THROTTLE_MS;
    
    // Check if client is disconnected and needs reconnection with new token
    bool needs_reconnect = !_analyzer_eu_client->connected();
    
    if (token_needs_renewal && can_attempt_renewal) {
      _last_token_renewal_attempt_eu = now_millis;
      
      // Prepare owner public key (if set) - convert to uppercase hex
      const char* owner_key = nullptr;
      char owner_key_uppercase[65];
      if (_prefs->mqtt_owner_public_key[0] != '\0') {
        // Copy and convert to uppercase
        strncpy(owner_key_uppercase, _prefs->mqtt_owner_public_key, sizeof(owner_key_uppercase) - 1);
        owner_key_uppercase[sizeof(owner_key_uppercase) - 1] = '\0';
        for (int i = 0; owner_key_uppercase[i]; i++) {
          owner_key_uppercase[i] = toupper(owner_key_uppercase[i]);
        }
        owner_key = owner_key_uppercase;
      }
      
      // Build client version string
      char client_version[64];
      getClientVersion(client_version, sizeof(client_version));
      
      // Get email from preferences (if set)
      const char* email = nullptr;
      if (_prefs->mqtt_email[0] != '\0') {
        email = _prefs->mqtt_email;
      }
      
      // Store old expiration time before renewing (to check if we need to disconnect)
      unsigned long old_token_expires_at = _token_eu_expires_at;
      
      // Renew the token
      if (JWTHelper::createAuthToken(
          *_identity, "mqtt-eu-v1.letsmesh.net", 
          0, 86400, _auth_token_eu, sizeof(_auth_token_eu),
          owner_key, client_version, email)) {
        unsigned long expires_in = 86400; // 24 hours
        _token_eu_expires_at = time_synced ? (current_time + expires_in) : 0;
        MQTT_DEBUG_PRINTLN("EU token renewed");
        
        _analyzer_eu_client->setCredentials(_analyzer_username, _auth_token_eu);
        
        bool old_token_expired_or_imminent = !time_synced || 
                                            (old_token_expires_at == 0) ||
                                            (current_time >= old_token_expires_at) ||
                                            (time_synced && old_token_expires_at >= 1000000000 && 
                                             current_time >= (old_token_expires_at - DISCONNECT_THRESHOLD));
        
        if (old_token_expired_or_imminent && _analyzer_eu_client->connected()) {
          _analyzer_eu_client->disconnect();
          _last_reconnect_attempt_eu = now_millis;
          _analyzer_eu_client->connect();
        } else if (!_analyzer_eu_client->connected()) {
          _last_reconnect_attempt_eu = now_millis;
          _analyzer_eu_client->connect();
        }
      } else {
        MQTT_DEBUG_PRINTLN("Failed to renew EU token");
        _token_eu_expires_at = 0;
      }
    } else if (needs_reconnect) {
      unsigned long reconnect_elapsed = (now_millis >= _last_reconnect_attempt_eu) ?
                                      (now_millis - _last_reconnect_attempt_eu) :
                                      (ULONG_MAX - _last_reconnect_attempt_eu + now_millis + 1);
      if (reconnect_elapsed >= RECONNECT_THROTTLE_MS) {
        _last_reconnect_attempt_eu = now_millis;
        _analyzer_eu_client->connect();
      }
    }
  }
  
  // Note: PsychicMqttClient handles automatic reconnection internally,
  // but we need to ensure tokens are renewed before reconnection attempts
}

void MQTTBridge::setMessageTypes(bool status, bool packets, bool raw) {
  _status_enabled = status;
  _packets_enabled = packets;
  _raw_enabled = raw;
}

int MQTTBridge::getConnectedBrokers() const {
  int count = 0;
  for (int i = 0; i < MAX_MQTT_BROKERS_COUNT; i++) {
    if (_brokers[i].enabled && _brokers[i].connected) {
      count++;
    }
  }
  return count;
}

int MQTTBridge::getQueueSize() const {
  #ifdef ESP_PLATFORM
  // Get actual queue size from FreeRTOS queue
  if (_packet_queue_handle != nullptr) {
    return uxQueueMessagesWaiting(_packet_queue_handle);
  }
  return 0;
  #else
  return _queue_count;
  #endif
}

void MQTTBridge::setStatsSources(mesh::Dispatcher* dispatcher, mesh::Radio* radio, 
                                  mesh::MainBoard* board, mesh::MillisecondClock* ms) {
  _dispatcher = dispatcher;
  _radio = radio;
  _board = board;
  _ms = ms;
}

void MQTTBridge::syncTimeWithNTP() {
  if (!WiFi.isConnected()) {
    MQTT_DEBUG_PRINTLN("Cannot sync time - WiFi not connected");
    return;
  }
  
  // Prevent multiple simultaneous NTP syncs
  // Check if we're already synced and sync was recent (within last 5 seconds)
  unsigned long now = millis();
  if (_ntp_synced && (now - _last_ntp_sync) < 5000) {
    // Already synced recently, skip
    return;
  }
  
  // Set flag to prevent concurrent syncs
  static bool sync_in_progress = false;
  if (sync_in_progress) {
    return;  // Another sync is already in progress
  }
  sync_in_progress = true;
  
  MQTT_DEBUG_PRINTLN("Syncing time with NTP...");
  
  // Test DNS resolution before attempting NTP sync
  #ifdef ESP_PLATFORM
  IPAddress resolved_ip;
  if (!WiFi.hostByName("pool.ntp.org", resolved_ip)) {
    MQTT_DEBUG_PRINTLN("WARNING: DNS resolution failed for pool.ntp.org - NTP sync may fail");
  }
  #endif
  
  // Begin NTP client
  _ntp_client.begin();
  
  // Force update (blocking call with timeout)
  if (_ntp_client.forceUpdate()) {
    unsigned long epochTime = _ntp_client.getEpochTime();
    
    // Set system timezone to UTC first
    // This ensures time() returns UTC time
    configTime(0, 0, "pool.ntp.org");
    
    // Update the device's RTC clock with UTC time (if available)
    if (_rtc) {
      _rtc->setCurrentTime(epochTime);
    }
    
    // Mark NTP as synced regardless of RTC availability
    // JWT tokens need valid time, which is now available via time()
    bool was_ntp_synced = _ntp_synced;
    _ntp_synced = true;
    _last_ntp_sync = millis();
    sync_in_progress = false;  // Clear sync flag
    
    MQTT_DEBUG_PRINTLN("Time synced: %lu", epochTime);
    
    if (!was_ntp_synced) {
      unsigned long current_time = time(nullptr);
      unsigned long expires_in = 86400; // 24 hours
      
      // If tokens were created before NTP sync (expires_at == 0), set expiration times now
      if (_analyzer_us_enabled && _token_us_expires_at == 0 && strlen(_auth_token_us) > 0) {
        _token_us_expires_at = current_time + expires_in;
        MQTT_DEBUG_PRINTLN("US token expiration set after NTP sync: %lu", _token_us_expires_at);
      }
      
      if (_analyzer_eu_enabled && _token_eu_expires_at == 0 && strlen(_auth_token_eu) > 0) {
        _token_eu_expires_at = current_time + expires_in;
      }
      
      // If tokens don't exist yet (deferred during begin()), create them now
      if ((_analyzer_us_enabled || _analyzer_eu_enabled) && 
          (strlen(_auth_token_us) == 0 && strlen(_auth_token_eu) == 0)) {
        if (createAuthToken()) {
          if (_analyzer_us_enabled && _analyzer_us_client && strlen(_auth_token_us) > 0) {
            _analyzer_us_client->setCredentials(_analyzer_username, _auth_token_us);
            if (!_analyzer_us_client->connected()) {
              _analyzer_us_client->connect();
            }
          }
          if (_analyzer_eu_enabled && _analyzer_eu_client && strlen(_auth_token_eu) > 0) {
            _analyzer_eu_client->setCredentials(_analyzer_username, _auth_token_eu);
            if (!_analyzer_eu_client->connected()) {
              _analyzer_eu_client->connect();
            }
          }
        } else {
          MQTT_DEBUG_PRINTLN("Failed to create tokens after NTP sync");
        }
      }
    }
    
    sync_in_progress = false;  // Clear sync flag on failure too
    
    // Set timezone from string (with DST support) - only if changed
    static char last_timezone[64] = "";
    if (strcmp(_prefs->timezone_string, last_timezone) != 0) {
      // Clean up old timezone object to prevent memory leak
      if (_timezone) {
        delete _timezone;
        _timezone = nullptr;
      }
      
      // Create timezone object based on timezone string
      Timezone* tz = createTimezoneFromString(_prefs->timezone_string);
      if (tz) {
        _timezone = tz;
      } else {
        // Create UTC timezone as fallback
        TimeChangeRule utc = {"UTC", Last, Sun, Mar, 0, 0};
        _timezone = new Timezone(utc, utc);
      }
      
      strncpy(last_timezone, _prefs->timezone_string, sizeof(last_timezone) - 1);
      last_timezone[sizeof(last_timezone) - 1] = '\0';
      
      // Force memory defragmentation after timezone recreation
      void* temp = malloc(1024);
      if (temp) {
        free(temp);
      }
    }
    
    // Get current time info
    struct tm* utc_timeinfo = gmtime((time_t*)&epochTime);
    struct tm* local_timeinfo = localtime((time_t*)&epochTime);
    (void)utc_timeinfo; // Unused but kept for debugging if needed
    (void)local_timeinfo;
  } else {
    MQTT_DEBUG_PRINTLN("NTP sync failed");
    sync_in_progress = false;  // Clear sync flag on failure
  }
  
  _ntp_client.end();
}

Timezone* MQTTBridge::createTimezoneFromString(const char* tz_string) {
  // Create Timezone objects for common IANA timezone strings
  
  // North America
  if (strcmp(tz_string, "America/Los_Angeles") == 0 || strcmp(tz_string, "America/Vancouver") == 0) {
    TimeChangeRule pst = {"PST", First, Sun, Nov, 2, -480};  // UTC-8
    TimeChangeRule pdt = {"PDT", Second, Sun, Mar, 2, -420}; // UTC-7
    return new Timezone(pdt, pst);
  } else if (strcmp(tz_string, "America/Denver") == 0) {
    TimeChangeRule mst = {"MST", First, Sun, Nov, 2, -420};  // UTC-7
    TimeChangeRule mdt = {"MDT", Second, Sun, Mar, 2, -360};  // UTC-6
    return new Timezone(mdt, mst);
  } else if (strcmp(tz_string, "America/Chicago") == 0) {
    TimeChangeRule cst = {"CST", First, Sun, Nov, 2, -360};  // UTC-6
    TimeChangeRule cdt = {"CDT", Second, Sun, Mar, 2, -300}; // UTC-5
    return new Timezone(cdt, cst);
  } else if (strcmp(tz_string, "America/New_York") == 0 || strcmp(tz_string, "America/Toronto") == 0) {
    TimeChangeRule est = {"EST", First, Sun, Nov, 2, -300};   // UTC-5
    TimeChangeRule edt = {"EDT", Second, Sun, Mar, 2, -240}; // UTC-4
    return new Timezone(edt, est);
  } else if (strcmp(tz_string, "America/Anchorage") == 0) {
    TimeChangeRule akst = {"AKST", First, Sun, Nov, 2, -540}; // UTC-9
    TimeChangeRule akdt = {"AKDT", Second, Sun, Mar, 2, -480}; // UTC-8
    return new Timezone(akdt, akst);
  } else if (strcmp(tz_string, "Pacific/Honolulu") == 0) {
    TimeChangeRule hst = {"HST", Last, Sun, Oct, 2, -600}; // UTC-10 (no DST)
    return new Timezone(hst, hst);
  
  // Europe
  } else if (strcmp(tz_string, "Europe/London") == 0) {
    TimeChangeRule gmt = {"GMT", Last, Sun, Oct, 2, 0};     // UTC+0
    TimeChangeRule bst = {"BST", Last, Sun, Mar, 1, 60};    // UTC+1
    return new Timezone(bst, gmt);
  } else if (strcmp(tz_string, "Europe/Paris") == 0 || strcmp(tz_string, "Europe/Berlin") == 0) {
    TimeChangeRule cet = {"CET", Last, Sun, Oct, 3, 60};    // UTC+1
    TimeChangeRule cest = {"CEST", Last, Sun, Mar, 2, 120}; // UTC+2
    return new Timezone(cest, cet);
  } else if (strcmp(tz_string, "Europe/Moscow") == 0) {
    TimeChangeRule msk = {"MSK", Last, Sun, Oct, 3, 180};   // UTC+3 (no DST since 2014)
    return new Timezone(msk, msk);
  
  // Asia
  } else if (strcmp(tz_string, "Asia/Tokyo") == 0) {
    TimeChangeRule jst = {"JST", Last, Sun, Oct, 2, 540};   // UTC+9 (no DST)
    return new Timezone(jst, jst);
  } else if (strcmp(tz_string, "Asia/Shanghai") == 0 || strcmp(tz_string, "Asia/Hong_Kong") == 0) {
    TimeChangeRule cst = {"CST", Last, Sun, Oct, 2, 480};   // UTC+8 (no DST)
    return new Timezone(cst, cst);
  } else if (strcmp(tz_string, "Asia/Kolkata") == 0) {
    TimeChangeRule ist = {"IST", Last, Sun, Oct, 2, 330};   // UTC+5:30 (no DST)
    return new Timezone(ist, ist);
  } else if (strcmp(tz_string, "Asia/Dubai") == 0) {
    TimeChangeRule gst = {"GST", Last, Sun, Oct, 2, 240};   // UTC+4 (no DST)
    return new Timezone(gst, gst);
  
  // Australia
  } else if (strcmp(tz_string, "Australia/Sydney") == 0 || strcmp(tz_string, "Australia/Melbourne") == 0) {
    TimeChangeRule aest = {"AEST", First, Sun, Apr, 3, 600};  // UTC+10
    TimeChangeRule aedt = {"AEDT", First, Sun, Oct, 2, 660};   // UTC+11
    return new Timezone(aedt, aest);
  } else if (strcmp(tz_string, "Australia/Perth") == 0) {
    TimeChangeRule awst = {"AWST", Last, Sun, Oct, 2, 480};   // UTC+8 (no DST)
    return new Timezone(awst, awst);
  
  // Timezone abbreviations (with DST handling)
  } else if (strcmp(tz_string, "PDT") == 0 || strcmp(tz_string, "PST") == 0) {
    // Pacific Time (PST/PDT)
    TimeChangeRule pst = {"PST", First, Sun, Nov, 2, -480};  // UTC-8
    TimeChangeRule pdt = {"PDT", Second, Sun, Mar, 2, -420}; // UTC-7
    return new Timezone(pdt, pst);
  } else if (strcmp(tz_string, "MDT") == 0 || strcmp(tz_string, "MST") == 0) {
    // Mountain Time (MST/MDT)
    TimeChangeRule mst = {"MST", First, Sun, Nov, 2, -420};  // UTC-7
    TimeChangeRule mdt = {"MDT", Second, Sun, Mar, 2, -360};  // UTC-6
    return new Timezone(mdt, mst);
  } else if (strcmp(tz_string, "CDT") == 0 || strcmp(tz_string, "CST") == 0) {
    // Central Time (CST/CDT)
    TimeChangeRule cst = {"CST", First, Sun, Nov, 2, -360};  // UTC-6
    TimeChangeRule cdt = {"CDT", Second, Sun, Mar, 2, -300}; // UTC-5
    return new Timezone(cdt, cst);
  } else if (strcmp(tz_string, "EDT") == 0 || strcmp(tz_string, "EST") == 0) {
    // Eastern Time (EST/EDT)
    TimeChangeRule est = {"EST", First, Sun, Nov, 2, -300};   // UTC-5
    TimeChangeRule edt = {"EDT", Second, Sun, Mar, 2, -240}; // UTC-4
    return new Timezone(edt, est);
  } else if (strcmp(tz_string, "BST") == 0 || strcmp(tz_string, "GMT") == 0) {
    // British Time (GMT/BST)
    TimeChangeRule gmt = {"GMT", Last, Sun, Oct, 2, 0};     // UTC+0
    TimeChangeRule bst = {"BST", Last, Sun, Mar, 1, 60};    // UTC+1
    return new Timezone(bst, gmt);
  } else if (strcmp(tz_string, "CEST") == 0 || strcmp(tz_string, "CET") == 0) {
    // Central European Time (CET/CEST)
    TimeChangeRule cet = {"CET", Last, Sun, Oct, 3, 60};    // UTC+1
    TimeChangeRule cest = {"CEST", Last, Sun, Mar, 2, 120}; // UTC+2
    return new Timezone(cest, cet);
  
  // UTC and simple offsets
  } else if (strcmp(tz_string, "UTC") == 0) {
    TimeChangeRule utc = {"UTC", Last, Sun, Mar, 0, 0};
    return new Timezone(utc, utc);
  } else if (strncmp(tz_string, "UTC", 3) == 0) {
    // Handle UTC+/-X format (UTC-8, UTC+5, etc.)
    int offset = atoi(tz_string + 3);
    TimeChangeRule utc_offset = {"UTC", Last, Sun, Mar, 0, offset * 60};
    return new Timezone(utc_offset, utc_offset);
  } else if (strncmp(tz_string, "GMT", 3) == 0) {
    // Handle GMT+/-X format (GMT-8, GMT+5, etc.)
    int offset = atoi(tz_string + 3);
    TimeChangeRule gmt_offset = {"GMT", Last, Sun, Mar, 0, offset * 60};
    return new Timezone(gmt_offset, gmt_offset);
  } else if (strncmp(tz_string, "+", 1) == 0 || strncmp(tz_string, "-", 1) == 0) {
    // Handle simple +/-X format (+5, -8, etc.)
    int offset = atoi(tz_string);
    TimeChangeRule offset_tz = {"TZ", Last, Sun, Mar, 0, offset * 60};
    return new Timezone(offset_tz, offset_tz);
  } else {
    // Unknown timezone, return null
    MQTT_DEBUG_PRINTLN("Unknown timezone: %s", tz_string);
    return nullptr;
  }
}

void MQTTBridge::getClientVersion(char* buffer, size_t buffer_size) const {
  if (!buffer || buffer_size == 0) {
    return;
  }
  // Generate client version string in format "meshcore/{firmware_version}"
  snprintf(buffer, buffer_size, "meshcore/%s", _firmware_version);
}

void MQTTBridge::optimizeMqttClientConfig(PsychicMqttClient* client, bool is_analyzer_client) {
  if (!client) return;
  
  // Buffer size selection (optimized for memory):
  // - Analyzer clients: Need 896 bytes for CONNECT message with 768-byte JWT tokens
  //   (CONNECT message: ~10 bytes overhead + 70 bytes username + 768 bytes password = ~850 bytes)
  //   Reduced from 1024 to 896 (128 bytes saved) - still safe with ~46 bytes headroom
  // - Main client: Can use 640 bytes (smaller than default 768, but safe for regular publishes)
  //   Most JSON messages are <500 bytes, CONNECT messages are smaller without JWT tokens
  //   Reduced from 768 to 640 (128 bytes saved) - still safe with ~140 bytes headroom
  int buffer_size = is_analyzer_client ? 896 : 640;
  
  client->setBufferSize(buffer_size);
  
  // Access ESP-IDF config to optimize additional settings
  esp_mqtt_client_config_t* config = client->getMqttConfig();
  if (config) {
    #if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
      if (config->buffer.out_size == 0 || config->buffer.out_size > buffer_size) {
        config->buffer.out_size = buffer_size;
      }
    #endif
  }
}

void MQTTBridge::logMemoryStatus() {
  MQTT_DEBUG_PRINTLN("Memory: Free=%d, Max=%d, Queue=%d/%d", 
                     ESP.getFreeHeap(), ESP.getMaxAllocHeap(), _queue_count, MAX_QUEUE_SIZE);
}

#endif

