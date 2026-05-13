#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Configuration structure for WiFi and MQTT
struct MqttConfig {
  char wifi_ssid[33];           // WiFi SSID (max 32 chars + null)
  char wifi_password[65];       // WiFi password (max 64 chars + null)
  char mqtt_server[129];        // MQTT broker address (max 128 chars + null)
  uint16_t mqtt_port;           // MQTT broker port
  char mqtt_user[65];           // MQTT username (optional, max 64 chars + null)
  char mqtt_password[65];       // MQTT password (optional, max 64 chars + null)
  char mqtt_topic_prefix[65];   // Base topic prefix (max 64 chars + null)
  bool enabled;                 // Enable/disable MQTT feature
};

class MqttClient {
private:
  WiFiClient wifiClient;
  PubSubClient mqttClient;
  MqttConfig config;
  bool connected;
  unsigned long lastReconnectAttempt;
  unsigned long reconnectDelay;
  char clientId[32];
  
  void publishMessage(const char* topic, const char* payload);
  void reconnect();
  
public:
  MqttClient();
  
  void begin(const MqttConfig& cfg, const char* nodeId);
  void loop();
  bool isConnected() const { return connected && mqttClient.connected(); }
  
  // Message publishing methods
  void onIncomingMessage(const char* fromName, const char* fromPubKeyPrefix, const char* text, float snr, float rssi);
  void onOutgoingMessage(const char* toName, const char* toPubKeyPrefix, const char* text);
  void onChannelMessage(const char* channelName, const char* senderName, const char* text, float snr, float rssi);
  
  // Connection management
  void disconnect();
};

extern MqttClient mqtt_client;

#if MQTT_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define MQTT_DEBUG_PRINT(F, ...) Serial.printf("MQTT: " F, ##__VA_ARGS__)
  #define MQTT_DEBUG_PRINTLN(F, ...) Serial.printf("MQTT: " F "\n", ##__VA_ARGS__)
#else
  #define MQTT_DEBUG_PRINT(...) {}
  #define MQTT_DEBUG_PRINTLN(...) {}
#endif
