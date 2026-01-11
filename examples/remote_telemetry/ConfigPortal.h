#pragma once

#include <WiFiManager.h>

#include "TelemetryConfig.h"

class ConfigPortal {
public:
  explicit ConfigPortal(telemetry::ConfigStore& store);

  bool ensureConfigured(bool forcePortal = false);

private:
  static constexpr const char* AP_NAME = "MeshcoreSetup";
  static constexpr size_t MQTT_FIELD_LEN = 128;
  static constexpr size_t USER_FIELD_LEN = 64;
  static constexpr size_t PASS_FIELD_LEN = 64;
  static constexpr size_t TOPIC_FIELD_LEN = 128;
  static constexpr size_t NUMERIC_FIELD_LEN = 16;
  static constexpr size_t REPEATER_JSON_LEN = 2048;

  telemetry::ConfigStore& _store;

  char _mqttHost[MQTT_FIELD_LEN];
  char _mqttPort[NUMERIC_FIELD_LEN];
  char _mqttUsername[USER_FIELD_LEN];
  char _mqttPassword[PASS_FIELD_LEN];
  char _mqttTelemetryTopic[TOPIC_FIELD_LEN];
  char _mqttStatusTopic[TOPIC_FIELD_LEN];
  char _mqttControlTopic[TOPIC_FIELD_LEN];
  char _pollInterval[NUMERIC_FIELD_LEN];
  char _loginRetry[NUMERIC_FIELD_LEN];
  char _timeoutRetry[NUMERIC_FIELD_LEN];
  char _repeatersJson[REPEATER_JSON_LEN];

  void populateBuffers();
  bool captureParameters(const WiFiManagerParameter& hostParam,
                         const WiFiManagerParameter& portParam,
                         const WiFiManagerParameter& userParam,
                         const WiFiManagerParameter& passParam,
                         const WiFiManagerParameter& telemetryParam,
                         const WiFiManagerParameter& statusParam,
                         const WiFiManagerParameter& controlParam,
                         const WiFiManagerParameter& pollParam,
                         const WiFiManagerParameter& loginParam,
                         const WiFiManagerParameter& timeoutParam,
                         const WiFiManagerParameter& repeatersParam);
};
