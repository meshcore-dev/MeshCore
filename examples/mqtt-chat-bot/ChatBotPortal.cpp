#include "ChatBotPortal.h"

#include <Arduino.h>
#include <cstring>

namespace {
void copyString(char* dest, size_t destLen, const String& value) {
  if (!dest || destLen == 0) {
    return;
  }
  value.toCharArray(dest, destLen);
}
}

ChatBotPortal::ChatBotPortal(chatbot::ConfigStore& store)
  : _store(store) {
  memset(_mqttHost, 0, sizeof(_mqttHost));
  memset(_mqttPort, 0, sizeof(_mqttPort));
  memset(_mqttUsername, 0, sizeof(_mqttUsername));
  memset(_mqttPassword, 0, sizeof(_mqttPassword));
  memset(_mqttControlTopic, 0, sizeof(_mqttControlTopic));
  memset(_mqttRxTopic, 0, sizeof(_mqttRxTopic));
  memset(_mqttTxTopic, 0, sizeof(_mqttTxTopic));
  memset(_meshNodeName, 0, sizeof(_meshNodeName));
  memset(_meshChannelName, 0, sizeof(_meshChannelName));
  memset(_meshChannelKey, 0, sizeof(_meshChannelKey));
}

void ChatBotPortal::populateBuffers() {
  const chatbot::Settings& settings = _store.data();

  copyString(_mqttHost, sizeof(_mqttHost), settings.mqttHost);
  snprintf(_mqttPort, sizeof(_mqttPort), "%u", settings.mqttPort == 0 ? 1883 : settings.mqttPort);
  copyString(_mqttUsername, sizeof(_mqttUsername), settings.mqttUsername);
  copyString(_mqttPassword, sizeof(_mqttPassword), settings.mqttPassword);
  copyString(_mqttControlTopic, sizeof(_mqttControlTopic), settings.mqttControlTopic);
  copyString(_mqttRxTopic, sizeof(_mqttRxTopic), settings.mqttRxTopic);
  copyString(_mqttTxTopic, sizeof(_mqttTxTopic), settings.mqttTxTopic);
  copyString(_meshNodeName, sizeof(_meshNodeName), settings.meshNodeName);
  copyString(_meshChannelName, sizeof(_meshChannelName), settings.meshChannelName);
  copyString(_meshChannelKey, sizeof(_meshChannelKey), settings.meshChannelKey);
}

unsigned long ChatBotPortal::parseUnsigned(const char* value, unsigned long fallback) {
  if (!value || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  unsigned long parsed = strtoul(value, &end, 10);
  if (end == value) {
    return fallback;
  }
  return parsed;
}

bool ChatBotPortal::captureParameters(const WiFiManagerParameter& hostParam,
                                      const WiFiManagerParameter& portParam,
                                      const WiFiManagerParameter& usernameParam,
                                      const WiFiManagerParameter& passwordParam,
                                      const WiFiManagerParameter& controlParam,
                                      const WiFiManagerParameter& rxParam,
                                      const WiFiManagerParameter& txParam,
                                      const WiFiManagerParameter& nodeParam,
                                      const WiFiManagerParameter& channelNameParam,
                                      const WiFiManagerParameter& channelKeyParam) {
  chatbot::Settings& settings = _store.data();

  settings.mqttHost = hostParam.getValue();
  settings.mqttHost.trim();
  settings.mqttUsername = usernameParam.getValue();
  settings.mqttUsername.trim();
  settings.mqttPassword = passwordParam.getValue();

  settings.mqttControlTopic = controlParam.getValue();
  settings.mqttControlTopic.trim();
  settings.mqttRxTopic = rxParam.getValue();
  settings.mqttRxTopic.trim();
  settings.mqttTxTopic = txParam.getValue();
  settings.mqttTxTopic.trim();

  unsigned long port = parseUnsigned(portParam.getValue(), settings.mqttPort == 0 ? 1883UL : settings.mqttPort);
  if (port == 0 || port > 65535UL) {
    port = 1883UL;
  }
  settings.mqttPort = static_cast<uint16_t>(port);

  settings.meshNodeName = nodeParam.getValue();
  settings.meshNodeName.trim();
  if (settings.meshNodeName.length() == 0) {
    settings.meshNodeName = "MeshBot";
  }

  settings.meshChannelName = channelNameParam.getValue();
  settings.meshChannelName.trim();
  if (settings.meshChannelName.length() == 0) {
    settings.meshChannelName = "IT-Telemetry";
  }

  String key = chatbot::normalizeChannelKey(String(channelKeyParam.getValue()));
  if (!chatbot::isChannelKeyValid(key)) {
    return false;
  }
  settings.meshChannelKey = key;

  settings.wifiSsid = WiFi.SSID();
  settings.wifiPassword = WiFi.psk();

  return true;
}

bool ChatBotPortal::ensureConfigured(bool forcePortal) {
  WiFi.mode(WIFI_STA);

  bool portalRequired = forcePortal || !_store.data().isValid();

  while (true) {
    populateBuffers();

    WiFiManagerParameter sectionMqtt("<hr><h3>MQTT</h3>");
    WiFiManagerParameter hostParam("mqtt_host", "MQTT host", _mqttHost, sizeof(_mqttHost));
    WiFiManagerParameter portParam("mqtt_port", "MQTT port", _mqttPort, sizeof(_mqttPort));
    WiFiManagerParameter userParam("mqtt_user", "MQTT username", _mqttUsername, sizeof(_mqttUsername));
    WiFiManagerParameter passParam("mqtt_pass", "MQTT password", _mqttPassword, sizeof(_mqttPassword));
    WiFiManagerParameter controlParam("mqtt_control", "MQTT control topic", _mqttControlTopic, sizeof(_mqttControlTopic));
    WiFiManagerParameter rxParam("mqtt_rx", "MQTT RX topic", _mqttRxTopic, sizeof(_mqttRxTopic));
    WiFiManagerParameter txParam("mqtt_tx", "MQTT TX topic", _mqttTxTopic, sizeof(_mqttTxTopic));

    WiFiManagerParameter sectionMesh("<hr><h3>Mesh Channel</h3>");
    WiFiManagerParameter nodeParam("mesh_node", "Mesh node name", _meshNodeName, sizeof(_meshNodeName));
    WiFiManagerParameter channelNameParam("mesh_channel", "Mesh channel name", _meshChannelName, sizeof(_meshChannelName));
    WiFiManagerParameter channelKeyParam("mesh_key", "Mesh channel key (hex)", _meshChannelKey, sizeof(_meshChannelKey));

    WiFiManager manager;
    manager.setDebugOutput(false);
    manager.setConfigPortalBlocking(true);
    manager.setBreakAfterConfig(true);

    manager.addParameter(&sectionMqtt);
    manager.addParameter(&hostParam);
    manager.addParameter(&portParam);
    manager.addParameter(&userParam);
    manager.addParameter(&passParam);
    manager.addParameter(&controlParam);
    manager.addParameter(&rxParam);
    manager.addParameter(&txParam);
    manager.addParameter(&sectionMesh);
    manager.addParameter(&nodeParam);
    manager.addParameter(&channelNameParam);
    manager.addParameter(&channelKeyParam);

    bool connected = false;
    if (portalRequired) {
      Serial.println(F("[chatbot] Starting configuration portal"));
      connected = manager.startConfigPortal(AP_NAME);
    } else {
      connected = manager.autoConnect(AP_NAME);
      if (!connected) {
        portalRequired = true;
        continue;
      }
    }

    if (!connected) {
      Serial.println(F("[chatbot] WiFi configuration aborted"));
      return false;
    }

    if (!captureParameters(hostParam, portParam, userParam, passParam, controlParam, rxParam, txParam, nodeParam, channelNameParam, channelKeyParam)) {
      Serial.println(F("[chatbot] Invalid configuration, reopening portal"));
      portalRequired = true;
      continue;
    }

    if (!_store.data().isValid()) {
      Serial.println(F("[chatbot] Configuration incomplete, reopening portal"));
      portalRequired = true;
      continue;
    }

    if (!_store.save()) {
      Serial.println(F("[chatbot] Failed to persist configuration"));
    }

    return true;
  }
}
