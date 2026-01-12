#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "ChatBotConfig.h"

class ChatBotMesh;

class ChatBotManager {
public:
  ChatBotManager(ChatBotMesh& mesh, PubSubClient& mqttClient, chatbot::Settings& settings);

  void begin();
  void loop();
  void attachConfigStore(chatbot::ConfigStore& store);

  void handleMeshChannelMessage(uint32_t timestamp, const char* text, bool direct);
  void notifySendTimeout();

private:
  ChatBotMesh& _mesh;
  PubSubClient& _mqtt;
  chatbot::Settings* _settings;
  chatbot::ConfigStore* _store;
  unsigned long _nextWifiRetry;
  unsigned long _nextMqttRetry;
  bool _subscriptionsActive;
  bool _wifiStatusLogged;
  String _clientId;
  String _lastControlTopic;
  String _lastTxTopic;

  static ChatBotManager* _instance;

  static void mqttCallback(char* topic, uint8_t* payload, unsigned int length);

  void onMqttMessage(const char* topic, const uint8_t* payload, size_t length);
  void handleControlMessage(const String& payload);
  void handleTxMessage(const String& payload);

  bool ensureWifi();
  bool ensureMqtt();
  void subscribeTopics();
  void resetSubscriptions();

  void publishStatus(const char* event, const char* detail = nullptr, const char* correlationId = nullptr);
  void publishConfigSnapshot(const char* correlationId = nullptr);
  void publishError(const char* code, const char* detail = nullptr, const char* correlationId = nullptr);

  bool applyConfigUpdate(JsonObjectConst data, String& error);
  void refreshClientId();

  static String buildHexSuffix(const uint8_t* data, size_t len);
};
