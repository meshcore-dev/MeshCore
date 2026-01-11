#include "ConfigPortal.h"

#include <Arduino.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace {
void copyString(char* dest, size_t destLen, const String& value) {
  if (!dest || destLen == 0) {
    return;
  }
  value.toCharArray(dest, destLen);
}

void copyCString(char* dest, size_t destLen, const char* value) {
  if (!dest || destLen == 0) {
    return;
  }
  if (!value) {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, value, destLen - 1);
  dest[destLen - 1] = '\0';
}

unsigned long parseUnsigned(const char* input, unsigned long fallback) {
  if (!input || input[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  unsigned long value = strtoul(input, &end, 10);
  if (end == input || value == 0) {
    return fallback;
  }
  return value;
}

}  // namespace

ConfigPortal::ConfigPortal(telemetry::ConfigStore& store)
  : _store(store) {
  memset(_mqttHost, 0, sizeof(_mqttHost));
  memset(_mqttPort, 0, sizeof(_mqttPort));
  memset(_mqttUsername, 0, sizeof(_mqttUsername));
  memset(_mqttPassword, 0, sizeof(_mqttPassword));
  memset(_mqttTelemetryTopic, 0, sizeof(_mqttTelemetryTopic));
  memset(_mqttStatusTopic, 0, sizeof(_mqttStatusTopic));
  memset(_mqttControlTopic, 0, sizeof(_mqttControlTopic));
  memset(_pollInterval, 0, sizeof(_pollInterval));
  memset(_loginRetry, 0, sizeof(_loginRetry));
  memset(_timeoutRetry, 0, sizeof(_timeoutRetry));
  memset(_repeatersJson, 0, sizeof(_repeatersJson));
}

void ConfigPortal::populateBuffers() {
  const telemetry::Settings& settings = _store.data();

  copyString(_mqttHost, sizeof(_mqttHost), settings.mqttHost);
  snprintf(_mqttPort, sizeof(_mqttPort), "%u", settings.mqttPort == 0 ? 1883 : settings.mqttPort);
  copyString(_mqttUsername, sizeof(_mqttUsername), settings.mqttUsername);
  copyString(_mqttPassword, sizeof(_mqttPassword), settings.mqttPassword);
  copyString(_mqttTelemetryTopic, sizeof(_mqttTelemetryTopic), settings.mqttTelemetryTopic);
  copyString(_mqttStatusTopic, sizeof(_mqttStatusTopic), settings.mqttStatusTopic);
  copyString(_mqttControlTopic, sizeof(_mqttControlTopic), settings.mqttControlTopic);
  snprintf(_pollInterval, sizeof(_pollInterval), "%lu", settings.pollIntervalMs);
  snprintf(_loginRetry, sizeof(_loginRetry), "%lu", settings.loginRetryMs);
  snprintf(_timeoutRetry, sizeof(_timeoutRetry), "%lu", settings.timeoutRetryMs);

  String repeatersJson = telemetry::repeatersToJson(settings.repeaters);
  copyString(_repeatersJson, sizeof(_repeatersJson), repeatersJson);
}

bool ConfigPortal::captureParameters(const WiFiManagerParameter& hostParam,
                                     const WiFiManagerParameter& portParam,
                                     const WiFiManagerParameter& userParam,
                                     const WiFiManagerParameter& passParam,
                                     const WiFiManagerParameter& telemetryParam,
                                     const WiFiManagerParameter& statusParam,
                                     const WiFiManagerParameter& controlParam,
                                     const WiFiManagerParameter& pollParam,
                                     const WiFiManagerParameter& loginParam,
                                     const WiFiManagerParameter& timeoutParam,
                                     const WiFiManagerParameter& repeatersParam) {
  telemetry::Settings& settings = _store.data();

  settings.mqttHost = hostParam.getValue();
  settings.mqttHost.trim();
  settings.mqttUsername = userParam.getValue();
  settings.mqttPassword = passParam.getValue();
  settings.mqttTelemetryTopic = telemetryParam.getValue();
  settings.mqttTelemetryTopic.trim();
  settings.mqttStatusTopic = statusParam.getValue();
  settings.mqttStatusTopic.trim();
  settings.mqttControlTopic = controlParam.getValue();
  settings.mqttControlTopic.trim();

  unsigned long port = parseUnsigned(portParam.getValue(), 1883UL);
  if (port > 65535UL) {
    port = 1883UL;
  }
  settings.mqttPort = static_cast<uint16_t>(port);

  settings.pollIntervalMs = parseUnsigned(pollParam.getValue(), settings.pollIntervalMs);
  settings.loginRetryMs = parseUnsigned(loginParam.getValue(), settings.loginRetryMs);
  settings.timeoutRetryMs = parseUnsigned(timeoutParam.getValue(), settings.timeoutRetryMs);

  String parseError;
  std::vector<telemetry::RepeaterConfig> parsed;
  if (!telemetry::parseRepeatersJson(repeatersParam.getValue(), parsed, parseError)) {
    Serial.println(F("[config] Repeater list invalid"));
    Serial.println(parseError);
    return false;
  }
  settings.repeaters = std::move(parsed);

  return true;
}

bool ConfigPortal::ensureConfigured(bool forcePortal) {
  WiFi.mode(WIFI_STA);

  bool portalRequired = forcePortal || !_store.data().isValid();

  while (true) {
    populateBuffers();

    WiFiManagerParameter sectionMqtt("<hr><h3>MQTT Settings</h3>");
    WiFiManagerParameter hostParam("mqtt_host", "MQTT host", _mqttHost, sizeof(_mqttHost));
    WiFiManagerParameter portParam("mqtt_port", "MQTT port", _mqttPort, sizeof(_mqttPort));
    WiFiManagerParameter userParam("mqtt_user", "MQTT username", _mqttUsername, sizeof(_mqttUsername));
    WiFiManagerParameter passParam("mqtt_pass", "MQTT password", _mqttPassword, sizeof(_mqttPassword));
    WiFiManagerParameter telemetryParam("mqtt_topic", "Telemetry topic", _mqttTelemetryTopic, sizeof(_mqttTelemetryTopic));
    WiFiManagerParameter statusParam("mqtt_status", "Status topic", _mqttStatusTopic, sizeof(_mqttStatusTopic));
    WiFiManagerParameter controlParam("mqtt_control", "Control topic", _mqttControlTopic, sizeof(_mqttControlTopic));

    WiFiManagerParameter sectionIntervals("<hr><h3>Request Timing</h3>");
    WiFiManagerParameter pollParam("poll_ms", "Telemetry interval (ms)", _pollInterval, sizeof(_pollInterval));
    WiFiManagerParameter loginParam("login_ms", "Login retry (ms)", _loginRetry, sizeof(_loginRetry));
    WiFiManagerParameter timeoutParam("timeout_ms", "Timeout retry (ms)", _timeoutRetry, sizeof(_timeoutRetry));

    WiFiManagerParameter sectionRepeaters("<hr><h3>Repeater Configuration</h3>");
    WiFiManagerParameter repeatersParam("repeaters", "Repeaters JSON", _repeatersJson, sizeof(_repeatersJson), "type=\"textarea\" rows=\"8\" style=\"width:100%\"");
    WiFiManagerParameter repeatersHelp("<p>Example: [{&quot;name&quot;:&quot;Node&quot;,&quot;password&quot;:&quot;secret&quot;,&quot;pubKey&quot;:&quot;001122...&quot;}]</p>");

    WiFiManager manager;
    manager.setDebugOutput(false);
    manager.setConfigPortalBlocking(true);
    manager.setBreakAfterConfig(true);
    manager.addParameter(&sectionMqtt);
    manager.addParameter(&hostParam);
    manager.addParameter(&portParam);
    manager.addParameter(&userParam);
    manager.addParameter(&passParam);
    manager.addParameter(&telemetryParam);
    manager.addParameter(&statusParam);
    manager.addParameter(&controlParam);
    manager.addParameter(&sectionIntervals);
    manager.addParameter(&pollParam);
    manager.addParameter(&loginParam);
    manager.addParameter(&timeoutParam);
    manager.addParameter(&sectionRepeaters);
    manager.addParameter(&repeatersParam);
    manager.addParameter(&repeatersHelp);

    bool connected = false;
    if (portalRequired) {
      Serial.println(F("[config] Starting WiFi manager portal"));
      connected = manager.startConfigPortal(AP_NAME);
    } else {
      connected = manager.autoConnect(AP_NAME);
      if (!connected) {
        portalRequired = true;
        continue;
      }
    }

    if (!connected) {
      Serial.println(F("[config] WiFi configuration aborted"));
      return false;
    }

    if (!captureParameters(hostParam, portParam, userParam, passParam, telemetryParam, statusParam, controlParam, pollParam, loginParam, timeoutParam, repeatersParam)) {
      portalRequired = true;
      continue;
    }

    if (!_store.data().isValid()) {
      Serial.println(F("[config] Configuration incomplete, reopening portal"));
      portalRequired = true;
      continue;
    }

    if (!_store.save()) {
      Serial.println(F("[config] Failed to persist configuration"));
    }

    return true;
  }
}
