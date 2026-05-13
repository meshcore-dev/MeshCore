#include "MqttClient.h"
#include <ArduinoJson.h>

MqttClient mqtt_client;

MqttClient::MqttClient() : mqttClient(wifiClient) {
  connected = false;
  lastReconnectAttempt = 0;
  reconnectDelay = 5000;
  memset(&config, 0, sizeof(config));
  memset(clientId, 0, sizeof(clientId));
}

void MqttClient::begin(const MqttConfig& cfg, const char* nodeId) {
  config = cfg;
  
  if (!config.enabled) {
    MQTT_DEBUG_PRINTLN("MQTT is disabled in configuration");
    return;
  }
  
  // Generate client ID from node ID
  snprintf(clientId, sizeof(clientId), "meshcore-%s", nodeId);
  
  // Configure MQTT client
  mqttClient.setServer(config.mqtt_server, config.mqtt_port);
  mqttClient.setBufferSize(2048);  // Increase buffer for larger messages
  
  // Start WiFi connection
  MQTT_DEBUG_PRINT("Connecting to WiFi: %s ... ", config.wifi_ssid);
  WiFi.begin(config.wifi_ssid, config.wifi_password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    MQTT_DEBUG_PRINT(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    MQTT_DEBUG_PRINTLN("\nWiFi connected successfully");
    MQTT_DEBUG_PRINT("IP address: ");
    MQTT_DEBUG_PRINTLN(WiFi.localIP().toString());
  } else {
    MQTT_DEBUG_PRINTLN("\nWiFi connection failed!");
    return;
  }
  
  connected = true;
  reconnect();
}

void MqttClient::reconnect() {
  if (!connected || mqttClient.connected()) {
    return;
  }
  
  unsigned long now = millis();
  if (now - lastReconnectAttempt < reconnectDelay) {
    return;
  }
  
  lastReconnectAttempt = now;
  
  MQTT_DEBUG_PRINT("Attempting MQTT connection to %s:%d ... ", config.mqtt_server, config.mqtt_port);
  
  // Attempt to connect
  bool result;
  if (strlen(config.mqtt_user) > 0) {
    result = mqttClient.connect(clientId, config.mqtt_user, config.mqtt_password);
  } else {
    result = mqttClient.connect(clientId);
  }
  
  if (result) {
    MQTT_DEBUG_PRINTLN("connected!");
    reconnectDelay = 5000;  // Reset delay on success
  } else {
    MQTT_DEBUG_PRINT("failed, rc=%d ", mqttClient.state());
    MQTT_DEBUG_PRINT("retrying in %d seconds\n", reconnectDelay / 1000);
    reconnectDelay = min(reconnectDelay * 2, 60000UL);  // Exponential backoff, max 60s
  }
}

void MqttClient::loop() {
  if (!connected || !config.enabled) {
    return;
  }
  
  // Maintain MQTT connection
  if (!mqttClient.connected()) {
    reconnect();
    return;
  }
  
  mqttClient.loop();
  
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    MQTT_DEBUG_PRINTLN("WiFi disconnected, attempting to reconnect...");
    connected = false;
    WiFi.begin(config.wifi_ssid, config.wifi_password);
  }
}

void MqttClient::publishMessage(const char* topic, const char* payload) {
  if (!connected || !mqttClient.connected()) {
    MQTT_DEBUG_PRINTLN("Cannot publish: not connected to MQTT broker");
    return;
  }
  
  MQTT_DEBUG_PRINT("Publishing to topic: %s\n", topic);
  MQTT_DEBUG_PRINT("Payload: %s\n", payload);
  
  if (mqttClient.publish(topic, payload, false)) {
    MQTT_DEBUG_PRINTLN("Published successfully");
  } else {
    MQTT_DEBUG_PRINTLN("Publish failed");
  }
}

void MqttClient::onIncomingMessage(const char* fromName, const char* fromPubKeyPrefix, const char* text, float snr, float rssi) {
  if (!config.enabled) return;
  
  StaticJsonDocument<512> doc;
  doc["type"] = "incoming_message";
  doc["from_name"] = fromName;
  doc["from_pubkey_prefix"] = fromPubKeyPrefix;
  doc["text"] = text;
  doc["snr"] = snr;
  doc["rssi"] = rssi;
  doc["timestamp"] = millis();
  
  char jsonBuffer[1024];
  serializeJson(doc, jsonBuffer);
  
  char topic[256];
  snprintf(topic, sizeof(topic), "%s/messages/incoming", config.mqtt_topic_prefix);
  publishMessage(topic, jsonBuffer);
}

void MqttClient::onOutgoingMessage(const char* toName, const char* toPubKeyPrefix, const char* text) {
  if (!config.enabled) return;
  
  StaticJsonDocument<512> doc;
  doc["type"] = "outgoing_message";
  doc["to_name"] = toName;
  doc["to_pubkey_prefix"] = toPubKeyPrefix;
  doc["text"] = text;
  doc["timestamp"] = millis();
  
  char jsonBuffer[1024];
  serializeJson(doc, jsonBuffer);
  
  char topic[256];
  snprintf(topic, sizeof(topic), "%s/messages/outgoing", config.mqtt_topic_prefix);
  publishMessage(topic, jsonBuffer);
}

void MqttClient::onChannelMessage(const char* channelName, const char* senderName, const char* text, float snr, float rssi) {
  if (!config.enabled) return;
  
  StaticJsonDocument<512> doc;
  doc["type"] = "channel_message";
  doc["channel"] = channelName;
  doc["sender_name"] = senderName;
  doc["text"] = text;
  doc["snr"] = snr;
  doc["rssi"] = rssi;
  doc["timestamp"] = millis();
  
  char jsonBuffer[1024];
  serializeJson(doc, jsonBuffer);
  
  char topic[256];
  snprintf(topic, sizeof(topic), "%s/messages/channel", config.mqtt_topic_prefix);
  publishMessage(topic, jsonBuffer);
}

void MqttClient::disconnect() {
  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  connected = false;
  MQTT_DEBUG_PRINTLN("MQTT disconnected");
}
