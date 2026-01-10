#include "RemoteTelemetryManager.h"

#include <ArduinoJson.h>
#include <esp_system.h>

#include <algorithm>
#include <cstring>

#include <Utils.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>

#if REMOTE_TELEMETRY_DEBUG
#define RT_DEBUG_PRINTF(F, ...) Serial.printf("[telemetry] " F, ##__VA_ARGS__)
#define RT_DEBUG_PRINTLN(F, ...) Serial.printf("[telemetry] " F "\n", ##__VA_ARGS__)
#define RT_INFO_PRINTLN(F, ...) Serial.printf("[telemetry] " F "\n", ##__VA_ARGS__)
#else
#define RT_DEBUG_PRINTF(...) do { } while (0)
#define RT_DEBUG_PRINTLN(...) do { } while (0)
#define RT_INFO_PRINTLN(...) do { } while (0)
#endif

namespace {
constexpr unsigned long LOGIN_RETRY_INTERVAL_MS = 120000UL;      // 2 minutes
constexpr unsigned long TELEMETRY_INTERVAL_MS = 30UL * 60UL * 1000UL;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 10000UL;
constexpr unsigned long REQUEST_GRACE_MS = 1000UL;
constexpr unsigned long DAILY_REBOOT_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
}

RemoteTelemetryManager::RemoteTelemetryManager(RemoteTelemetryMesh& mesh, PubSubClient& mqttClient)
  : _mesh(mesh), _mqtt(mqttClient), _repeaterCount(0), _debugEnabled(REMOTE_TELEMETRY_DEBUG != 0),
    _bootMillis(0), _nextWifiRetry(0), _nextMqttRetry(0) {
  memset(_repeaters, 0, sizeof(_repeaters));
  _mesh.setManager(this);
}

void RemoteTelemetryManager::begin() {
  _bootMillis = millis();
  _mesh.begin();
  configureRepeaters();

  WiFi.mode(WIFI_STA);
  ensureWifi();
  ensureMqtt();
}

void RemoteTelemetryManager::loop() {
  ensureWifi();
  ensureMqtt();
  processRepeaters();
  checkRebootWindow();
}

void RemoteTelemetryManager::handleLoginResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) {
  int idx = findRepeaterIndex(contact);
  if (idx < 0) {
    RT_DEBUG_PRINTLN("Login response from unknown contact");
    return;
  }

  auto& state = _repeaters[idx];
  state.loginPending = false;

  if (len >= 6 && (data[4] == REMOTE_RESP_SERVER_LOGIN_OK || memcmp(&data[4], "OK", 2) == 0)) {
    state.loggedIn = true;
    state.lastLoginSuccess = millis();
    RT_INFO_PRINTLN("Login succeeded for %s", state.creds->name);
    scheduleLogin(state, TELEMETRY_INTERVAL_MS); // ensure fallback if repeater drops session
    requestTelemetry(state, true);
  } else {
    RT_INFO_PRINTLN("Login response without success code for %s", state.creds->name);
    state.loggedIn = false;
    scheduleLogin(state, LOGIN_RETRY_INTERVAL_MS);
  }
}

void RemoteTelemetryManager::handleTelemetryResponse(const ContactInfo& contact, uint32_t tag, const uint8_t* payload, uint8_t len) {
  int idx = findRepeaterIndex(contact);
  if (idx < 0) {
    RT_DEBUG_PRINTLN("Telemetry response from unknown contact");
    return;
  }

  auto& state = _repeaters[idx];
  if (!state.telemetryPending || state.pendingTelemetryTag != tag) {
    RT_DEBUG_PRINTLN("Unexpected telemetry tag for %s", state.creds->name);
    return;
  }

  state.telemetryPending = false;
  state.pendingTelemetryTag = 0;
  state.nextTelemetryPoll = millis() + TELEMETRY_INTERVAL_MS;

  publishTelemetry(state, tag, payload, len);
}

void RemoteTelemetryManager::notifySendTimeout() {
  unsigned long now = millis();
  for (size_t i = 0; i < _repeaterCount; i++) {
    auto& state = _repeaters[i];
    if (state.telemetryPending && now > state.telemetryDeadline) {
      RT_INFO_PRINTLN("Telemetry request timed out for %s", state.creds->name);
      state.telemetryPending = false;
      state.pendingTelemetryTag = 0;
      state.nextTelemetryPoll = now + LOGIN_RETRY_INTERVAL_MS;
    }
    if (state.loginPending && now > state.loginDeadline) {
      RT_INFO_PRINTLN("Login request timed out for %s", state.creds->name);
      state.loginPending = false;
      state.loggedIn = false;
      scheduleLogin(state, LOGIN_RETRY_INTERVAL_MS);
    }
  }
}

void RemoteTelemetryManager::configureRepeaters() {
  _repeaterCount = std::min(NUM_REPEATER_CREDENTIALS, (size_t)REMOTE_TELEMETRY_MAX_REPEATERS);
  for (size_t i = 0; i < _repeaterCount; i++) {
    const auto& cfg = REPEATER_CREDENTIALS[i];
    ContactInfo contact = {};
    memcpy(contact.id.pub_key, cfg.pub_key, PUB_KEY_SIZE);
    contact.type = ADV_TYPE_REPEATER;
    contact.flags = 0;
    contact.out_path_len = -1;
    contact.last_advert_timestamp = 0;
    contact.lastmod = 0;
    contact.gps_lat = contact.gps_lon = 0;
    contact.sync_since = 0;
    StrHelper::strzcpy(contact.name, cfg.name ? cfg.name : "repeater", sizeof(contact.name));

    if (!_mesh.addContact(contact)) {
      RT_INFO_PRINTLN("Failed to add contact for %s", cfg.name);
      continue;
    }

    auto stored = _mesh.lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE);
    if (!stored) {
      RT_INFO_PRINTLN("Unable to lookup stored contact for %s", cfg.name);
      continue;
    }

    auto& state = _repeaters[i];
    state.creds = &cfg;
    state.contact = stored;
    state.loginPending = false;
    state.loggedIn = false;
    state.telemetryPending = false;
    state.pendingTelemetryTag = 0;
    state.loginDeadline = 0;
    state.telemetryDeadline = 0;
    state.nextLoginAttempt = millis();
    state.nextTelemetryPoll = 0;
    state.lastLoginSuccess = 0;

    RT_INFO_PRINTLN("Configured repeater %s", cfg.name);
  }
}

void RemoteTelemetryManager::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  if (now < _nextWifiRetry) {
    return;
  }

  RT_INFO_PRINTLN("Connecting to WiFi SSID %s", WIFI_CREDENTIALS.ssid);
  WiFi.begin(WIFI_CREDENTIALS.ssid, WIFI_CREDENTIALS.password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    RT_DEBUG_PRINTF(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    RT_INFO_PRINTLN("WiFi connected, IP %s", WiFi.localIP().toString().c_str());
  } else {
    RT_INFO_PRINTLN("WiFi connection failed");
    _nextWifiRetry = now + WIFI_CONNECT_TIMEOUT_MS;
  }
}

void RemoteTelemetryManager::ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (_mqtt.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now < _nextMqttRetry) {
    return;
  }

  char clientId[20];
  snprintf(clientId, sizeof(clientId), "mesh-%02X%02X", _mesh.self_id.pub_key[0], _mesh.self_id.pub_key[1]);

  const char* host = MQTT_CREDENTIALS.host;
  if (!host || host[0] == '\0') {
    RT_INFO_PRINTLN("MQTT host not configured");
    _nextMqttRetry = now + MQTT_RETRY_INTERVAL_MS;
    return;
  }
  if (strncmp(host, "mqtt://", 7) == 0) {
    host += 7;
  } else if (strncmp(host, "tcp://", 6) == 0) {
    host += 6;
  }
  uint16_t port = MQTT_CREDENTIALS.port == 0 ? 1883 : MQTT_CREDENTIALS.port;

  RT_INFO_PRINTLN("Connecting to MQTT %s:%u", host, port);
  _mqtt.setServer(host, port);
  if (_mqtt.connect(clientId, MQTT_CREDENTIALS.username, MQTT_CREDENTIALS.password)) {
    RT_INFO_PRINTLN("MQTT connected as %s", clientId);
  } else {
    RT_INFO_PRINTLN("MQTT connect failed, rc=%d", _mqtt.state());
    _nextMqttRetry = now + MQTT_RETRY_INTERVAL_MS;
  }
}

void RemoteTelemetryManager::processRepeaters() {
  unsigned long now = millis();
  for (size_t i = 0; i < _repeaterCount; i++) {
    auto& state = _repeaters[i];

    if (!state.creds || !state.contact) {
      continue;
    }

    if (!state.loggedIn && !state.loginPending && now >= state.nextLoginAttempt) {
      uint32_t est;
      int result = _mesh.sendLogin(*state.contact, state.creds->password, est);
      if (result == MSG_SEND_FAILED) {
        RT_INFO_PRINTLN("Unable to send login to %s", state.creds->name);
        scheduleLogin(state, LOGIN_RETRY_INTERVAL_MS);
      } else {
        state.loginPending = true;
        state.loginDeadline = now + est + REQUEST_GRACE_MS;
        RT_INFO_PRINTLN("Login sent to %s (%s)", state.creds->name, result == MSG_SEND_SENT_DIRECT ? "direct" : "flood");
      }
    }

    if (state.loggedIn && !state.telemetryPending && now >= state.nextTelemetryPoll) {
      requestTelemetry(state, false);
    }

    if (state.loginPending && now > state.loginDeadline) {
      RT_INFO_PRINTLN("Login timed out for %s", state.creds->name);
      state.loginPending = false;
      state.loggedIn = false;
      scheduleLogin(state, LOGIN_RETRY_INTERVAL_MS);
    }

    if (state.telemetryPending && now > state.telemetryDeadline) {
      RT_INFO_PRINTLN("Telemetry timed out for %s", state.creds->name);
      state.telemetryPending = false;
      state.pendingTelemetryTag = 0;
      state.nextTelemetryPoll = now + LOGIN_RETRY_INTERVAL_MS;
    }
  }
}

void RemoteTelemetryManager::scheduleLogin(RepeaterState& state, unsigned long delayMs) {
  state.nextLoginAttempt = millis() + delayMs;
}

void RemoteTelemetryManager::requestTelemetry(RepeaterState& state, bool immediate) {
  if (!state.loggedIn || state.telemetryPending) {
    return;
  }

  uint32_t tag;
  uint32_t est;
  int result = _mesh.sendRequest(*state.contact, REQ_TYPE_GET_TELEMETRY_DATA, tag, est);
  if (result == MSG_SEND_FAILED) {
    RT_INFO_PRINTLN("Failed to send telemetry request to %s", state.creds->name);
    state.nextTelemetryPoll = millis() + LOGIN_RETRY_INTERVAL_MS;
    return;
  }

  state.telemetryPending = true;
  state.pendingTelemetryTag = tag;
  state.telemetryDeadline = millis() + est + REQUEST_GRACE_MS;
  if (immediate) {
    state.nextTelemetryPoll = millis() + TELEMETRY_INTERVAL_MS;
  }
  RT_INFO_PRINTLN("Telemetry request sent to %s", state.creds->name);
}

void RemoteTelemetryManager::publishTelemetry(const RepeaterState& state, uint32_t tag, const uint8_t* payload, uint8_t len) {
  if (!_mqtt.connected()) {
    RT_INFO_PRINTLN("Skipping telemetry publish, MQTT offline");
    return;
  }

  if (!MQTT_CREDENTIALS.topic || MQTT_CREDENTIALS.topic[0] == '\0') {
    RT_INFO_PRINTLN("MQTT topic not configured, skipping publish");
    return;
  }

  StaticJsonDocument<768> doc;
  JsonObject root = doc.to<JsonObject>();
  root["tag"] = tag;
  root["received"] = millis();
  root["repeater"]["name"] = state.creds->name;

  char pubKeyHex[PUB_KEY_SIZE * 2 + 1];
  mesh::Utils::toHex(pubKeyHex, state.contact->id.pub_key, PUB_KEY_SIZE);
  root["repeater"]["pubKey"] = pubKeyHex;

  JsonArray fields = root.createNestedArray("measurements");
  LPPReader reader(payload, len);
  uint8_t channel;
  uint8_t type;
  while (reader.readHeader(channel, type)) {
    JsonObject meas = fields.createNestedObject();
    meas["channel"] = channel;
    meas["type"] = type;
    switch (type) {
      case LPP_VOLTAGE: {
        float v;
        if (reader.readVoltage(v)) {
          meas["label"] = "voltage";
          meas["value"] = v;
        }
        break;
      }
      case LPP_CURRENT: {
        float a;
        if (reader.readCurrent(a)) {
          meas["label"] = "current";
          meas["value"] = a;
        }
        break;
      }
      case LPP_POWER: {
        float w;
        if (reader.readPower(w)) {
          meas["label"] = "power";
          meas["value"] = w;
        }
        break;
      }
      case LPP_TEMPERATURE: {
        float t;
        if (reader.readTemperature(t)) {
          meas["label"] = "temperature";
          meas["value"] = t;
        }
        break;
      }
      case LPP_RELATIVE_HUMIDITY: {
        float h;
        if (reader.readRelativeHumidity(h)) {
          meas["label"] = "humidity";
          meas["value"] = h;
        }
        break;
      }
      case LPP_BAROMETRIC_PRESSURE: {
        float p;
        if (reader.readPressure(p)) {
          meas["label"] = "pressure";
          meas["value"] = p;
        }
        break;
      }
      case LPP_ALTITUDE: {
        float alt;
        if (reader.readAltitude(alt)) {
          meas["label"] = "altitude";
          meas["value"] = alt;
        }
        break;
      }
      case LPP_GPS: {
        float lat, lon, alt;
        if (reader.readGPS(lat, lon, alt)) {
          meas["label"] = "gps";
          meas["lat"] = lat;
          meas["lon"] = lon;
          meas["alt"] = alt;
        }
        break;
      }
      default:
        meas["label"] = "raw";
        reader.skipData(type);
        break;
    }
  }

  char buffer[768];
  size_t written = serializeJson(doc, buffer, sizeof(buffer));
  if (written == 0) {
    RT_INFO_PRINTLN("Failed to serialise telemetry JSON");
    return;
  }

  if (_mqtt.publish(MQTT_CREDENTIALS.topic, buffer, written)) {
    RT_INFO_PRINTLN("Telemetry published for %s", state.creds->name);
  } else {
    RT_INFO_PRINTLN("Failed to publish telemetry for %s", state.creds->name);
  }
}

int RemoteTelemetryManager::findRepeaterIndex(const ContactInfo& contact) const {
  const ContactInfo* ptr = &contact;
  for (size_t i = 0; i < _repeaterCount; i++) {
    if (_repeaters[i].contact == ptr) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void RemoteTelemetryManager::checkRebootWindow() {
  if (millis() - _bootMillis >= DAILY_REBOOT_INTERVAL_MS) {
    RT_INFO_PRINTLN("Rebooting after 24h watchdog");
    delay(100);
    esp_restart();
  }
}
