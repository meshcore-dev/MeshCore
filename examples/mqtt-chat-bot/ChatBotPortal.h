#pragma once

#include <WiFiManager.h>

#include "ChatBotConfig.h"

class ChatBotPortal {
public:
  explicit ChatBotPortal(chatbot::ConfigStore& store);

  bool ensureConfigured(bool forcePortal);

private:
  static constexpr const char* AP_NAME = "MeshcoreChatBot";
  static constexpr size_t MQTT_FIELD_LEN = 128;
  static constexpr size_t TOPIC_FIELD_LEN = 128;
  static constexpr size_t PORT_FIELD_LEN = 8;
  static constexpr size_t NAME_FIELD_LEN = 32;
  static constexpr size_t KEY_FIELD_LEN = 65;

  chatbot::ConfigStore& _store;
  char _mqttHost[MQTT_FIELD_LEN];
  char _mqttPort[PORT_FIELD_LEN];
  char _mqttUsername[MQTT_FIELD_LEN];
  char _mqttPassword[MQTT_FIELD_LEN];
  char _mqttControlTopic[TOPIC_FIELD_LEN];
  char _mqttRxTopic[TOPIC_FIELD_LEN];
  char _mqttTxTopic[TOPIC_FIELD_LEN];
  char _meshNodeName[NAME_FIELD_LEN];
  char _meshChannelName[NAME_FIELD_LEN];
  char _meshChannelKey[KEY_FIELD_LEN];

  void populateBuffers();
  bool captureParameters(const WiFiManagerParameter& hostParam,
                         const WiFiManagerParameter& portParam,
                         const WiFiManagerParameter& usernameParam,
                         const WiFiManagerParameter& passwordParam,
                         const WiFiManagerParameter& controlParam,
                         const WiFiManagerParameter& rxParam,
                         const WiFiManagerParameter& txParam,
                         const WiFiManagerParameter& nodeParam,
                         const WiFiManagerParameter& channelNameParam,
                         const WiFiManagerParameter& channelKeyParam);
  static unsigned long parseUnsigned(const char* value, unsigned long fallback);
};
