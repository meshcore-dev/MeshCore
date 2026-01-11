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
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 10000UL;
constexpr unsigned long REQUEST_GRACE_MS = 10000UL;
constexpr unsigned long DAILY_REBOOT_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
constexpr unsigned long MIN_INTERVAL_MS = 5000UL;
constexpr unsigned long MAX_POLL_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr unsigned long MAX_TIMEOUT_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr unsigned long MAX_LOGIN_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL;
constexpr unsigned long MIN_REQUEST_GAP_MS = 2000UL;
constexpr size_t INVALID_REQUEST_INDEX = static_cast<size_t>(-1);
constexpr size_t INVALID_REPEATER_INDEX = static_cast<size_t>(-1);
}

RemoteTelemetryManager* RemoteTelemetryManager::_instance = nullptr;

RemoteTelemetryManager::RemoteTelemetryManager(RemoteTelemetryMesh& mesh, PubSubClient& mqttClient, telemetry::Settings& settings)
  : _mesh(mesh), _mqtt(mqttClient), _settings(&settings), _configStore(nullptr), _repeaterCount(0), _debugEnabled(REMOTE_TELEMETRY_DEBUG != 0),
    _bootMillis(0), _nextWifiRetry(0), _nextMqttRetry(0),
    _loginRetryMs(0),
    _pollIntervalMs(0),
    _timeoutRetryMs(0),
    _statusPublished(false), _controlSubscribed(false),
    _nextRequestReady(0),
    _ntpConfigured(false), _timeSynced(false), _timeSyncAttempted(false),
    _nextTimeCheck(0), _lastTimeWaitLog(0),
    _activeRequestType(PendingRequestType::None),
    _activeRequestIndex(INVALID_REQUEST_INDEX) {
  memset(_repeaters, 0, sizeof(_repeaters));
  _mesh.setManager(this);
  _instance = this;
  _mqtt.setCallback(RemoteTelemetryManager::mqttCallback);
  applyIntervals();
}

void RemoteTelemetryManager::reloadSettings(telemetry::Settings& settings) {
  _settings = &settings;
  applyIntervals();
  configureRepeaters();
}

void RemoteTelemetryManager::attachConfigStore(telemetry::ConfigStore& store) {
  _configStore = &store;
  _settings = &_configStore->data();
  applyIntervals();
  configureRepeaters();
}

void RemoteTelemetryManager::applyIntervals() {
  auto clamp = [](unsigned long value, unsigned long minValue, unsigned long maxValue) {
    if (value < minValue) {
      return minValue;
    }
    if (value > maxValue) {
      return maxValue;
    }
    return value;
  };

  if (_settings) {
    _pollIntervalMs = clamp(_settings->pollIntervalMs, MIN_INTERVAL_MS, MAX_POLL_INTERVAL_MS);
    _loginRetryMs = clamp(_settings->loginRetryMs, MIN_INTERVAL_MS, MAX_LOGIN_INTERVAL_MS);
    _timeoutRetryMs = clamp(_settings->timeoutRetryMs, MIN_INTERVAL_MS, MAX_TIMEOUT_INTERVAL_MS);
  } else {
    _pollIntervalMs = clamp(30UL * 60UL * 1000UL, MIN_INTERVAL_MS, MAX_POLL_INTERVAL_MS);
    _loginRetryMs = clamp(120000UL, MIN_INTERVAL_MS, MAX_LOGIN_INTERVAL_MS);
    _timeoutRetryMs = clamp(30000UL, MIN_INTERVAL_MS, MAX_TIMEOUT_INTERVAL_MS);
  }
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
  if (state.configIndex == INVALID_REPEATER_INDEX) {
    RT_DEBUG_PRINTLN("Login response for repeater without configuration");
    return;
  }
  const auto& cfg = repeaterConfig(state);
  state.loginPending = false;

  LoginMode mode = state.pendingLoginMode;
  state.pendingLoginMode = LoginMode::None;

  if (state.lastLoginRequestSent != 0) {
    unsigned long rtt = millis() - state.lastLoginRequestSent;
    RT_DEBUG_PRINTLN("Login RTT for %s was %lu ms", cfg.name.c_str(), rtt);
  }
  state.lastLoginRequestSent = 0;
  markRequestCompleted(PendingRequestType::Login, static_cast<size_t>(idx));

  bool success = len >= 6 && (data[4] == REMOTE_RESP_SERVER_LOGIN_OK || memcmp(&data[4], "OK", 2) == 0);
  const char* modeLabel = (mode == LoginMode::Admin) ? "Admin" : "Guest";

  if (!success) {
    RT_INFO_PRINTLN("%s login response without success code for %s", modeLabel, cfg.name.c_str());
    if (mode == LoginMode::Guest) {
      state.guestRouteEstablished = false;
    }
    state.loggedIn = false;
    scheduleLogin(state, _loginRetryMs);
    return;
  }

  unsigned long now = millis();

  if (mode == LoginMode::Admin) {
    state.guestRouteEstablished = true;
    state.loggedIn = true;
    state.lastLoginSuccess = now;
    scheduleLogin(state, _pollIntervalMs);
    state.nextTelemetryPoll = now;
    RT_INFO_PRINTLN("Admin login succeeded for %s", cfg.name.c_str());
    return;
  }

  bool routeKnown = state.contact && state.contact->out_path_len >= 0;
  state.guestRouteEstablished = routeKnown;

  if (!routeKnown) {
    RT_INFO_PRINTLN("Guest login succeeded for %s but route not yet known", cfg.name.c_str());
    scheduleLogin(state, _loginRetryMs);
    return;
  }

  if (cfg.password.length() == 0) {
    state.loggedIn = true;
    state.lastLoginSuccess = now;
    scheduleLogin(state, _pollIntervalMs);
    state.nextTelemetryPoll = now;
    RT_INFO_PRINTLN("Guest login established telemetry session for %s", cfg.name.c_str());
  } else {
    state.loggedIn = false;
    state.nextLoginAttempt = now;
    RT_INFO_PRINTLN("Guest login established route for %s, queuing admin login", cfg.name.c_str());
  }
}

void RemoteTelemetryManager::handleTelemetryResponse(const ContactInfo& contact, uint32_t tag, const uint8_t* payload, uint8_t len) {
  int idx = findRepeaterIndex(contact);
  if (idx < 0) {
    RT_DEBUG_PRINTLN("Telemetry response from unknown contact");
    return;
  }

  auto& state = _repeaters[idx];
  if (state.configIndex == INVALID_REPEATER_INDEX) {
    RT_DEBUG_PRINTLN("Telemetry response for repeater without configuration");
    return;
  }
  const auto& cfg = repeaterConfig(state);
  if (!state.telemetryPending || state.pendingTelemetryTag != tag) {
    RT_DEBUG_PRINTLN("Unexpected telemetry tag for %s", cfg.name.c_str());
    return;
  }

  state.telemetryPending = false;
  state.pendingTelemetryTag = 0;
  unsigned long now = millis();
  state.nextTelemetryPoll = now + _pollIntervalMs;

  if (state.lastTelemetryRequestSent != 0) {
    unsigned long rtt = now - state.lastTelemetryRequestSent;
    RT_DEBUG_PRINTLN("Telemetry RTT for %s was %lu ms", cfg.name.c_str(), rtt);
    state.lastTelemetryRequestSent = 0;
  }

  publishTelemetry(state, tag, payload, len);
  markRequestCompleted(PendingRequestType::Telemetry, static_cast<size_t>(idx));
}

void RemoteTelemetryManager::notifySendTimeout() {
  unsigned long now = millis();
  for (size_t i = 0; i < _repeaterCount; i++) {
    auto& state = _repeaters[i];
    if (state.configIndex == INVALID_REPEATER_INDEX) {
      continue;
    }
    const auto& cfg = repeaterConfig(state);
    if (state.telemetryPending && now > state.telemetryDeadline) {
      RT_INFO_PRINTLN("Telemetry request timed out for %s", cfg.name.c_str());
      state.telemetryPending = false;
      state.pendingTelemetryTag = 0;
      state.nextTelemetryPoll = now + _timeoutRetryMs;
      if (state.lastTelemetryRequestSent != 0) {
        unsigned long waited = now - state.lastTelemetryRequestSent;
        RT_DEBUG_PRINTLN("Telemetry timeout after %lu ms for %s", waited, cfg.name.c_str());
      }
      state.lastTelemetryRequestSent = 0;
      markRequestCompleted(PendingRequestType::Telemetry, i);
    }
    if (state.loginPending && now > state.loginDeadline) {
      LoginMode mode = state.pendingLoginMode;
      state.pendingLoginMode = LoginMode::None;
      const char* modeLabel = (mode == LoginMode::Admin) ? "Admin" : "Guest";
      RT_INFO_PRINTLN("%s login request timed out for %s", modeLabel, cfg.name.c_str());
      state.loginPending = false;
      state.loggedIn = false;
      if (mode == LoginMode::Guest) {
        state.guestRouteEstablished = false;
      }
      scheduleLogin(state, _loginRetryMs);
      if (state.lastLoginRequestSent != 0) {
        unsigned long waited = now - state.lastLoginRequestSent;
        RT_DEBUG_PRINTLN("Login timeout after %lu ms for %s", waited, cfg.name.c_str());
      }
      state.lastLoginRequestSent = 0;
      markRequestCompleted(PendingRequestType::Login, i);
    }
  }
}

void RemoteTelemetryManager::configureRepeaters() {
  unsigned long now = millis();
  for (auto& state : _repeaters) {
    state.configIndex = INVALID_REPEATER_INDEX;
    state.contact = nullptr;
    state.loginPending = false;
    state.loggedIn = false;
    state.telemetryPending = false;
    state.pendingTelemetryTag = 0;
    state.guestRouteEstablished = false;
    state.loginDeadline = 0;
    state.telemetryDeadline = 0;
    state.nextLoginAttempt = now;
    state.nextTelemetryPoll = 0;
    state.lastLoginSuccess = 0;
    state.lastLoginRequestSent = 0;
    state.lastTelemetryRequestSent = 0;
    state.pendingLoginMode = LoginMode::None;
  }

  _repeaterCount = 0;
  if (!_settings) {
    RT_INFO_PRINTLN("No telemetry settings defined; repeater table empty");
    return;
  }

  const auto& configs = _settings->repeaters;
  size_t limit = std::min(configs.size(), static_cast<size_t>(REMOTE_TELEMETRY_MAX_REPEATERS));

  for (size_t i = 0; i < limit; ++i) {
    const auto& cfg = configs[i];

    const ContactInfo* stored = _mesh.lookupContactByPubKey(cfg.pubKey.data(), PUB_KEY_SIZE);
    if (!stored) {
      ContactInfo contact = {};
      memcpy(contact.id.pub_key, cfg.pubKey.data(), PUB_KEY_SIZE);
      contact.type = ADV_TYPE_REPEATER;
      contact.flags = 0;
      contact.out_path_len = -1;
      contact.last_advert_timestamp = 0;
      contact.lastmod = 0;
      contact.gps_lat = contact.gps_lon = 0;
      contact.sync_since = 0;
      StrHelper::strzcpy(contact.name, cfg.name.c_str(), sizeof(contact.name));

      if (!_mesh.addContact(contact)) {
        RT_INFO_PRINTLN("Failed to add contact for %s", cfg.name.c_str());
        continue;
      }

      stored = _mesh.lookupContactByPubKey(contact.id.pub_key, PUB_KEY_SIZE);
      if (!stored) {
        RT_INFO_PRINTLN("Unable to lookup stored contact for %s", cfg.name.c_str());
        continue;
      }
    }

    if (_repeaterCount >= REMOTE_TELEMETRY_MAX_REPEATERS) {
      break;
    }

    auto& state = _repeaters[_repeaterCount++];
    state.configIndex = i;
    state.contact = stored;

    RT_INFO_PRINTLN("Configured repeater %s", cfg.name.c_str());
  }
}

const telemetry::RepeaterConfig& RemoteTelemetryManager::repeaterConfig(const RepeaterState& state) const {
  static telemetry::RepeaterConfig empty;
  if (!_settings || state.configIndex == INVALID_REPEATER_INDEX || state.configIndex >= _settings->repeaters.size()) {
    return empty;
  }
  return _settings->repeaters[state.configIndex];
}

void RemoteTelemetryManager::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    ensureTimeSync();
    return;
  }

  unsigned long now = millis();
  if (now < _nextWifiRetry) {
    return;
  }

  RT_INFO_PRINTLN("Attempting WiFi reconnect");
  WiFi.reconnect();
  _nextWifiRetry = now + WIFI_CONNECT_TIMEOUT_MS;
}

void RemoteTelemetryManager::ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED) {
    _statusPublished = false;
    _controlSubscribed = false;
    return;
  }

  if (!_settings) {
    return;
  }

  if (!_mqtt.connected()) {
    unsigned long now = millis();
    if (now < _nextMqttRetry) {
      return;
    }

    _statusPublished = false;
    _controlSubscribed = false;

    char clientId[20];
    snprintf(clientId, sizeof(clientId), "mesh-%02X%02X", _mesh.self_id.pub_key[0], _mesh.self_id.pub_key[1]);

    String hostString = _settings->mqttHost;
    hostString.trim();
    if (hostString.length() == 0) {
      RT_INFO_PRINTLN("MQTT host not configured");
      _nextMqttRetry = now + MQTT_RETRY_INTERVAL_MS;
      return;
    }

    if (hostString.startsWith("mqtt://")) {
      hostString.remove(0, 7);
    } else if (hostString.startsWith("tcp://")) {
      hostString.remove(0, 6);
    }

    uint16_t port = _settings->mqttPort == 0 ? 1883 : _settings->mqttPort;

    RT_INFO_PRINTLN("Connecting to MQTT %s:%u", hostString.c_str(), port);
    _mqtt.setServer(hostString.c_str(), port);
    const char* username = _settings->mqttUsername.length() > 0 ? _settings->mqttUsername.c_str() : nullptr;
    const char* password = _settings->mqttPassword.length() > 0 ? _settings->mqttPassword.c_str() : nullptr;
    if (_mqtt.connect(clientId, username, password)) {
      RT_INFO_PRINTLN("MQTT connected as %s", clientId);
      if (_settings->mqttControlTopic.length() > 0) {
        if (_mqtt.subscribe(_settings->mqttControlTopic.c_str())) {
          _controlSubscribed = true;
          RT_INFO_PRINTLN("Subscribed to MQTT control topic %s", _settings->mqttControlTopic.c_str());
        } else {
          RT_INFO_PRINTLN("Failed to subscribe to MQTT control topic %s", _settings->mqttControlTopic.c_str());
        }
      }
      publishStatusEvent("boot", true);
    } else {
      RT_INFO_PRINTLN("MQTT connect failed, rc=%d", _mqtt.state());
      _nextMqttRetry = now + MQTT_RETRY_INTERVAL_MS;
    }
    return;
  }

  if (!_controlSubscribed && _settings->mqttControlTopic.length() > 0) {
    if (_mqtt.subscribe(_settings->mqttControlTopic.c_str())) {
      _controlSubscribed = true;
      RT_INFO_PRINTLN("Subscribed to MQTT control topic %s", _settings->mqttControlTopic.c_str());
    }
  }

  if (!_statusPublished) {
    publishStatusEvent("boot", true);
  }
}

void RemoteTelemetryManager::ensureTimeSync() {
  if (_timeSynced) {
    return;
  }

  unsigned long now = millis();

  if (!_ntpConfigured) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    _ntpConfigured = true;
    _timeSyncAttempted = true;
    _nextTimeCheck = now + 3000UL;
    RT_INFO_PRINTLN("Requested NTP sync");
    return;
  }

  if (now < _nextTimeCheck) {
    return;
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    _timeSynced = true;
    _lastTimeWaitLog = 0;
    RT_INFO_PRINTLN("Time synchronised via NTP");
  } else {
    _nextTimeCheck = now + 2000UL;
    if (now - _lastTimeWaitLog >= 5000UL) {
      RT_INFO_PRINTLN("Waiting for NTP sync...");
      _lastTimeWaitLog = now;
    }
  }
}

void RemoteTelemetryManager::processRepeaters() {
  unsigned long now = millis();

  if (!_timeSynced) {
    if (_timeSyncAttempted && now - _lastTimeWaitLog >= 5000UL) {
      RT_INFO_PRINTLN("Waiting for time sync before contacting repeaters");
      _lastTimeWaitLog = now;
    }
    return;
  }

  _lastTimeWaitLog = 0;

  for (size_t i = 0; i < _repeaterCount; i++) {
    auto& state = _repeaters[i];

    if (state.configIndex == INVALID_REPEATER_INDEX || !state.contact) {
      continue;
    }

    const auto& cfg = repeaterConfig(state);

    if (state.loginPending && now > state.loginDeadline) {
      LoginMode mode = state.pendingLoginMode;
      state.pendingLoginMode = LoginMode::None;
      const char* modeLabel = (mode == LoginMode::Admin) ? "Admin" : "Guest";
      RT_INFO_PRINTLN("%s login timed out for %s", modeLabel, cfg.name.c_str());
      state.loginPending = false;
      state.loggedIn = false;
      if (mode == LoginMode::Guest) {
        state.guestRouteEstablished = false;
      }
      if (state.lastLoginRequestSent != 0) {
        unsigned long waited = now - state.lastLoginRequestSent;
        RT_DEBUG_PRINTLN("Login timeout after %lu ms for %s", waited, cfg.name.c_str());
      }
      scheduleLogin(state, _loginRetryMs);
      markRequestCompleted(PendingRequestType::Login, i);
      state.lastLoginRequestSent = 0;
    }

    if (state.telemetryPending && now > state.telemetryDeadline) {
      RT_INFO_PRINTLN("Telemetry timed out for %s", cfg.name.c_str());
      state.telemetryPending = false;
      state.pendingTelemetryTag = 0;
      state.nextTelemetryPoll = now + _timeoutRetryMs;
      markRequestCompleted(PendingRequestType::Telemetry, i);
      if (state.lastTelemetryRequestSent != 0) {
        unsigned long waited = now - state.lastTelemetryRequestSent;
        RT_DEBUG_PRINTLN("Telemetry timeout after %lu ms for %s", waited, cfg.name.c_str());
      }
      state.lastTelemetryRequestSent = 0;
    }
  }

  if (_activeRequestType != PendingRequestType::None) {
    return;
  }

  if (now < _nextRequestReady) {
    return;
  }

  for (size_t i = 0; i < _repeaterCount; i++) {
    auto& state = _repeaters[i];
    if (state.configIndex == INVALID_REPEATER_INDEX || !state.contact) {
      continue;
    }
    if (!state.loggedIn || state.telemetryPending) {
      continue;
    }
    if (now < state.nextTelemetryPoll) {
      continue;
    }
    if (requestTelemetry(state, i)) {
      return;
    }
  }

  for (size_t i = 0; i < _repeaterCount; i++) {
    auto& state = _repeaters[i];
    if (state.configIndex == INVALID_REPEATER_INDEX || !state.contact) {
      continue;
    }
    if (state.loggedIn || state.loginPending) {
      continue;
    }
    if (now < state.nextLoginAttempt) {
      continue;
    }

    const auto& cfg = repeaterConfig(state);

    LoginMode nextMode = LoginMode::None;
    bool adminPasswordPresent = cfg.password.length() > 0;

    if (!state.guestRouteEstablished) {
      nextMode = LoginMode::Guest;
    } else if (adminPasswordPresent && !state.loggedIn) {
      if (state.contact->out_path_len < 0) {
        nextMode = LoginMode::Guest;
      } else {
        nextMode = LoginMode::Admin;
      }
    } else if (!adminPasswordPresent && !state.loggedIn) {
      nextMode = LoginMode::Guest;
    }

    if (nextMode == LoginMode::None) {
      continue;
    }

    const char* password = (nextMode == LoginMode::Admin) ? cfg.password.c_str() : "";

    uint32_t est;
    int result = _mesh.sendLogin(*state.contact, password, est);
    if (result == MSG_SEND_FAILED) {
      RT_INFO_PRINTLN("Unable to send login to %s", cfg.name.c_str());
      scheduleLogin(state, _loginRetryMs);
      deferNextRequest();
    } else {
      state.loginPending = true;
      state.loginDeadline = now + est + REQUEST_GRACE_MS;
      state.lastLoginRequestSent = now;
      state.pendingLoginMode = nextMode;
      markRequestStarted(PendingRequestType::Login, i);
      const char* modeLabel = (nextMode == LoginMode::Admin) ? "admin" : "guest";
      RT_DEBUG_PRINTLN("%s login send est=%lu ms deadline=%lu ms for %s", modeLabel, static_cast<unsigned long>(est), static_cast<unsigned long>(state.loginDeadline - now), cfg.name.c_str());
      RT_INFO_PRINTLN("%s login sent to %s (%s)", modeLabel, cfg.name.c_str(), result == MSG_SEND_SENT_DIRECT ? "direct" : "flood");
    }
    return;
  }
}

void RemoteTelemetryManager::scheduleLogin(RepeaterState& state, unsigned long delayMs) {
  state.nextLoginAttempt = millis() + delayMs;
}

bool RemoteTelemetryManager::requestTelemetry(RepeaterState& state, size_t index) {
  if (state.configIndex == INVALID_REPEATER_INDEX || !state.contact) {
    return false;
  }

  if (!state.loggedIn || state.telemetryPending) {
    return false;
  }

  const auto& cfg = repeaterConfig(state);

  unsigned long now = millis();
  if (!canIssueRequest(now)) {
    return false;
  }

  uint32_t tag;
  uint32_t est;
  int result = _mesh.sendRequest(*state.contact, REQ_TYPE_GET_TELEMETRY_DATA, tag, est);
  if (result == MSG_SEND_FAILED) {
    RT_INFO_PRINTLN("Failed to send telemetry request to %s", cfg.name.c_str());
    state.nextTelemetryPoll = now + _loginRetryMs;
    deferNextRequest();
    return false;
  }

  state.telemetryPending = true;
  state.pendingTelemetryTag = tag;
  state.telemetryDeadline = now + est + REQUEST_GRACE_MS;
  state.lastTelemetryRequestSent = now;
  state.nextTelemetryPoll = millis() + _pollIntervalMs;
  markRequestStarted(PendingRequestType::Telemetry, index);
  RT_DEBUG_PRINTLN("Telemetry send est=%lu ms deadline=%lu ms for %s", static_cast<unsigned long>(est), static_cast<unsigned long>(state.telemetryDeadline - now), cfg.name.c_str());
  RT_INFO_PRINTLN("Telemetry request sent to %s", cfg.name.c_str());
  return true;
}

void RemoteTelemetryManager::publishTelemetry(const RepeaterState& state, uint32_t tag, const uint8_t* payload, uint8_t len) {
  if (!_settings || state.configIndex == INVALID_REPEATER_INDEX) {
    RT_INFO_PRINTLN("Skipping telemetry publish, repeater configuration unavailable");
    return;
  }

  const auto& cfg = repeaterConfig(state);

  if (!_mqtt.connected()) {
    RT_INFO_PRINTLN("Skipping telemetry publish, MQTT offline");
    return;
  }

  if (_settings->mqttTelemetryTopic.length() == 0) {
    RT_INFO_PRINTLN("MQTT topic not configured, skipping publish");
    return;
  }

  StaticJsonDocument<768> doc;
  JsonObject root = doc.to<JsonObject>();
  root["tag"] = tag;
  root["received"] = millis();
  root["repeater"]["name"] = cfg.name;

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

  if (_mqtt.publish(_settings->mqttTelemetryTopic.c_str(), buffer, written)) {
    RT_INFO_PRINTLN("Telemetry published for %s", cfg.name.c_str());
  } else {
    RT_INFO_PRINTLN("Failed to publish telemetry for %s", cfg.name.c_str());
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

void RemoteTelemetryManager::onMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  if (!topic) {
    return;
  }

  if (_settings && _settings->mqttControlTopic.length() > 0 && strcmp(topic, _settings->mqttControlTopic.c_str()) == 0) {
    handleControlMessage(payload, length);
  } else {
#if REMOTE_TELEMETRY_DEBUG
    RT_DEBUG_PRINTLN("Ignoring MQTT message on topic %s", topic);
#endif
  }
}

void RemoteTelemetryManager::handleControlMessage(const uint8_t* payload, size_t length) {
  if (!payload || length == 0) {
    RT_INFO_PRINTLN("Received empty control message");
    return;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    RT_INFO_PRINTLN("Control message parse error: %s", err.c_str());
    publishStatusEvent("control_parse_error", false);
    return;
  }

  const char* command = nullptr;
  if (doc.containsKey("command")) {
    command = doc["command"].as<const char*>();
  } else if (doc.containsKey("action")) {
    command = doc["action"].as<const char*>();
  }

  if (command && command[0] != '\0') {
    handleConfigCommand(command, doc);
    return;
  }

  bool intervalUpdated = false;
  bool timeoutUpdated = false;
  bool loginUpdated = false;

  if (doc.containsKey("pollIntervalMs")) {
    unsigned long requested = doc["pollIntervalMs"].as<unsigned long>();
    intervalUpdated = applyPollInterval(requested);
  } else if (doc.containsKey("pollIntervalSeconds")) {
    unsigned long requestedSec = doc["pollIntervalSeconds"].as<unsigned long>();
    if (requestedSec > (MAX_POLL_INTERVAL_MS / 1000UL)) {
      requestedSec = (MAX_POLL_INTERVAL_MS / 1000UL);
    }
    intervalUpdated = applyPollInterval(requestedSec * 1000UL);
  }

  if (doc.containsKey("timeoutRetryMs")) {
    unsigned long requested = doc["timeoutRetryMs"].as<unsigned long>();
    if (requested < MIN_INTERVAL_MS) {
      requested = MIN_INTERVAL_MS;
    }
    if (requested > MAX_TIMEOUT_INTERVAL_MS) {
      requested = MAX_TIMEOUT_INTERVAL_MS;
    }
    _timeoutRetryMs = requested;
    if (_settings) {
      _settings->timeoutRetryMs = _timeoutRetryMs;
    }
    timeoutUpdated = true;
    RT_INFO_PRINTLN("Telemetry timeout retry interval updated to %lu ms", _timeoutRetryMs);
  } else if (doc.containsKey("timeoutRetrySeconds")) {
    unsigned long requestedSec = doc["timeoutRetrySeconds"].as<unsigned long>();
    if (requestedSec > (MAX_TIMEOUT_INTERVAL_MS / 1000UL)) {
      requestedSec = (MAX_TIMEOUT_INTERVAL_MS / 1000UL);
    }
    unsigned long requested = requestedSec * 1000UL;
    if (requested < MIN_INTERVAL_MS) {
      requested = MIN_INTERVAL_MS;
    }
    _timeoutRetryMs = requested;
    if (_settings) {
      _settings->timeoutRetryMs = _timeoutRetryMs;
    }
    timeoutUpdated = true;
    RT_INFO_PRINTLN("Telemetry timeout retry interval updated to %lu ms", _timeoutRetryMs);
  }

  if (doc.containsKey("loginRetryMs")) {
    unsigned long requested = doc["loginRetryMs"].as<unsigned long>();
    if (requested < MIN_INTERVAL_MS) {
      requested = MIN_INTERVAL_MS;
    }
    if (requested > MAX_LOGIN_INTERVAL_MS) {
      requested = MAX_LOGIN_INTERVAL_MS;
    }
    _loginRetryMs = requested;
    if (_settings) {
      _settings->loginRetryMs = _loginRetryMs;
    }
    loginUpdated = true;
    RT_INFO_PRINTLN("Login retry interval updated to %lu ms", _loginRetryMs);
  } else if (doc.containsKey("loginRetrySeconds")) {
    unsigned long requestedSec = doc["loginRetrySeconds"].as<unsigned long>();
    if (requestedSec > (MAX_LOGIN_INTERVAL_MS / 1000UL)) {
      requestedSec = (MAX_LOGIN_INTERVAL_MS / 1000UL);
    }
    unsigned long requested = requestedSec * 1000UL;
    if (requested < MIN_INTERVAL_MS) {
      requested = MIN_INTERVAL_MS;
    }
    _loginRetryMs = requested;
    if (_settings) {
      _settings->loginRetryMs = _loginRetryMs;
    }
    loginUpdated = true;
    RT_INFO_PRINTLN("Login retry interval updated to %lu ms", _loginRetryMs);
  }

  if (intervalUpdated || timeoutUpdated || loginUpdated) {
    if (loginUpdated) {
      unsigned long now = millis();
      for (size_t i = 0; i < _repeaterCount; i++) {
        auto& state = _repeaters[i];
        if (state.configIndex == INVALID_REPEATER_INDEX || !state.contact) {
          continue;
        }
        if (!state.loggedIn && !state.loginPending) {
          state.nextLoginAttempt = now;
        }
      }
    }
    publishStatusEvent("control_update", false);
  } else {
    publishStatusEvent("control_ack", false);
  }
}

void RemoteTelemetryManager::handleConfigCommand(const char* command, JsonDocument& doc) {
  if (!_settings) {
    publishStatusPayload("control_error", "settings_unavailable");
    return;
  }

  String detail;
  auto equals = [&](const char* value) -> bool {
    return value && strcmp(command, value) == 0;
  };

  if (equals("list_repeaters") || equals("get_repeaters") || equals("query_repeaters") || equals("get_config")) {
    if (publishRepeatersSnapshot("repeaters_snapshot", "config_sent")) {
      publishStatusPayload("control_ack", "repeaters_snapshot_sent");
    } else {
      publishStatusPayload("control_error", "repeaters_snapshot_failed");
    }
    return;
  }

  if (!_configStore) {
    publishStatusPayload("control_error", "config_store_missing");
    return;
  }

  auto& configs = _settings->repeaters;

  if (equals("add_repeater")) {
    JsonVariant repeaterVar;
    if (doc.containsKey("repeater")) {
      repeaterVar = doc["repeater"];
    } else {
      repeaterVar = doc.as<JsonVariant>();
    }

    String parseError;
    telemetry::RepeaterConfig cfg;
    if (!repeaterVar.is<JsonObjectConst>() || !decodeRepeaterConfig(repeaterVar.as<JsonObjectConst>(), cfg, parseError)) {
      detail = parseError.length() > 0 ? parseError : F("invalid_repeater_payload");
      publishStatusPayload("control_error", detail.c_str());
      return;
    }

    if (configs.size() >= REMOTE_TELEMETRY_MAX_REPEATERS) {
      publishStatusPayload("control_error", "repeater_limit_reached");
      return;
    }

    if (findConfigIndexByKey(cfg.pubKey) >= 0) {
      publishStatusPayload("control_error", "repeater_exists");
      return;
    }

    configs.push_back(cfg);
    if (!persistSettings("repeater_add")) {
      configs.pop_back();
      publishStatusPayload("control_error", "config_save_failed");
      return;
    }

    configureRepeaters();

    publishStatusPayload("repeater_added", cfg.name.c_str());
    publishRepeatersSnapshot("repeaters_snapshot", "repeater_added");
    return;
  }

  if (equals("remove_repeater") || equals("delete_repeater")) {
    const char* key = nullptr;
    if (doc.containsKey("pubKey")) {
      key = doc["pubKey"].as<const char*>();
    } else if (doc.containsKey("repeater")) {
      key = doc["repeater"]["pubKey"].as<const char*>();
    }

    if (!key) {
      publishStatusPayload("control_error", "pubKey_required");
      return;
    }

    std::array<uint8_t, PUB_KEY_SIZE> target;
    if (!telemetry::decodeHexKey(key, target)) {
      publishStatusPayload("control_error", "pubKey_invalid");
      return;
    }

    int idx = findConfigIndexByKey(target);
    if (idx < 0) {
      publishStatusPayload("control_error", "repeater_not_found");
      return;
    }

    telemetry::RepeaterConfig removed = configs[idx];
    configs.erase(configs.begin() + idx);
    if (!persistSettings("repeater_remove")) {
      configs.insert(configs.begin() + idx, removed);
      publishStatusPayload("control_error", "config_save_failed");
      return;
    }

    configureRepeaters();

    publishStatusPayload("repeater_removed", removed.name.c_str());
    publishRepeatersSnapshot("repeaters_snapshot", "repeater_removed");
    return;
  }

  if (equals("update_repeater") || equals("modify_repeater") || equals("change_repeater")) {
    JsonVariant repeaterVar;
    if (doc.containsKey("repeater")) {
      repeaterVar = doc["repeater"];
    } else {
      repeaterVar = doc.as<JsonVariant>();
    }

    if (!repeaterVar.is<JsonObjectConst>()) {
      publishStatusPayload("control_error", "repeater_payload_required");
      return;
    }

    JsonObjectConst obj = repeaterVar.as<JsonObjectConst>();
    const char* key = obj.containsKey("pubKey") ? obj["pubKey"].as<const char*>() : nullptr;
    if (!key && doc.containsKey("pubKey")) {
      key = doc["pubKey"].as<const char*>();
    }

    if (!key) {
      publishStatusPayload("control_error", "pubKey_required");
      return;
    }

    std::array<uint8_t, PUB_KEY_SIZE> target;
    if (!telemetry::decodeHexKey(key, target)) {
      publishStatusPayload("control_error", "pubKey_invalid");
      return;
    }

    int idx = findConfigIndexByKey(target);
    if (idx < 0) {
      publishStatusPayload("control_error", "repeater_not_found");
      return;
    }

    telemetry::RepeaterConfig original = configs[idx];
    telemetry::RepeaterConfig updated = original;

    if (obj.containsKey("name")) {
      updated.name = obj["name"].as<const char*>();
    }
    if (obj.containsKey("password")) {
      updated.password = obj["password"].as<const char*>();
    }
    if (obj.containsKey("newPubKey")) {
      const char* newKey = obj["newPubKey"].as<const char*>();
      std::array<uint8_t, PUB_KEY_SIZE> newKeyArray;
      if (!telemetry::decodeHexKey(newKey, newKeyArray)) {
        publishStatusPayload("control_error", "new_pubKey_invalid");
        return;
      }
      if (findConfigIndexByKey(newKeyArray) >= 0 && memcmp(newKeyArray.data(), updated.pubKey.data(), PUB_KEY_SIZE) != 0) {
        publishStatusPayload("control_error", "new_pubKey_conflict");
        return;
      }
      updated.pubKey = newKeyArray;
    }

    if (!updated.isValid()) {
      publishStatusPayload("control_error", "repeater_invalid");
      return;
    }

    configs[idx] = updated;
    if (!persistSettings("repeater_update")) {
      configs[idx] = original;
      publishStatusPayload("control_error", "config_save_failed");
      return;
    }

    configureRepeaters();

    publishStatusPayload("repeater_updated", updated.name.c_str());
    publishRepeatersSnapshot("repeaters_snapshot", "repeater_updated");
    return;
  }

  publishStatusPayload("control_error", "unknown_command");
}

bool RemoteTelemetryManager::applyPollInterval(unsigned long intervalMs) {
  if (intervalMs == 0) {
    return false;
  }

  if (intervalMs < MIN_INTERVAL_MS) {
    intervalMs = MIN_INTERVAL_MS;
  }

  if (intervalMs > MAX_POLL_INTERVAL_MS) {
    intervalMs = MAX_POLL_INTERVAL_MS;
  }

  if (intervalMs == _pollIntervalMs) {
    return false;
  }

  _pollIntervalMs = intervalMs;
  if (_settings) {
    _settings->pollIntervalMs = _pollIntervalMs;
  }

  unsigned long now = millis();
  for (size_t i = 0; i < _repeaterCount; i++) {
    auto& state = _repeaters[i];
    if (state.configIndex == INVALID_REPEATER_INDEX || !state.contact) {
      continue;
    }

    if (!state.telemetryPending) {
      state.nextTelemetryPoll = now;
    }

    if (state.loggedIn) {
      scheduleLogin(state, _pollIntervalMs);
    }
  }

  RT_INFO_PRINTLN("Telemetry poll interval updated to %lu ms", _pollIntervalMs);
  return true;
}

bool RemoteTelemetryManager::decodeRepeaterConfig(JsonVariantConst obj, telemetry::RepeaterConfig& out, String& error) {
  if (!obj.is<JsonObjectConst>()) {
    error = F("repeater_object_required");
    return false;
  }

  telemetry::RepeaterConfig candidate;
  candidate.name = obj["name"].as<const char*>();
  if (obj.containsKey("password")) {
    const char* pass = obj["password"].as<const char*>();
    candidate.password = pass ? pass : "";
  } else {
    candidate.password = "";
  }
  const char* key = obj["pubKey"].as<const char*>();
  if (!telemetry::decodeHexKey(key, candidate.pubKey)) {
    error = F("pubKey_invalid");
    return false;
  }

  if (!candidate.isValid()) {
    error = F("repeater_invalid");
    return false;
  }

  out = candidate;
  return true;
}

int RemoteTelemetryManager::findConfigIndexByKey(const std::array<uint8_t, PUB_KEY_SIZE>& key) const {
  if (!_settings) {
    return -1;
  }

  const auto& configs = _settings->repeaters;
  for (size_t i = 0; i < configs.size(); ++i) {
    if (memcmp(configs[i].pubKey.data(), key.data(), PUB_KEY_SIZE) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool RemoteTelemetryManager::persistSettings(const char* context) {
  if (!_configStore) {
    RT_INFO_PRINTLN("Config store unavailable for %s", context ? context : "update");
    return false;
  }
  if (!_configStore->save()) {
    RT_INFO_PRINTLN("Failed to persist configuration for %s", context ? context : "update");
    return false;
  }
  RT_INFO_PRINTLN("Configuration saved (%s)", context ? context : "update");
  return true;
}

bool RemoteTelemetryManager::publishRepeatersSnapshot(const char* event, const char* detail) {
  if (!_mqtt.connected() || !_settings || _settings->mqttStatusTopic.length() == 0) {
    return false;
  }

  StaticJsonDocument<1536> doc;
  doc["event"] = event ? event : "repeaters_snapshot";
  if (detail && detail[0] != '\0') {
    doc["detail"] = detail;
  }
  doc["uptimeMs"] = millis();

  JsonArray arr = doc.createNestedArray("repeaters");
  for (const auto& cfg : _settings->repeaters) {
    JsonObject repeater = arr.createNestedObject();
    repeater["name"] = cfg.name;
    repeater["password"] = cfg.password;
    repeater["pubKey"] = telemetry::encodeHexKey(cfg.pubKey);
  }

  char pubKeyHex[PUB_KEY_SIZE * 2 + 1];
  mesh::Utils::toHex(pubKeyHex, _mesh.self_id.pub_key, PUB_KEY_SIZE);
  doc["nodePubKey"] = pubKeyHex;

  char buffer[1536];
  size_t written = serializeJson(doc, buffer, sizeof(buffer));
  if (written == 0 || written >= sizeof(buffer)) {
    RT_INFO_PRINTLN("Failed to serialise repeaters snapshot");
    return false;
  }

  bool ok = _mqtt.publish(_settings->mqttStatusTopic.c_str(), buffer, written);
  if (!ok) {
    RT_INFO_PRINTLN("Failed to publish repeaters snapshot");
  }
  return ok;
}

bool RemoteTelemetryManager::publishStatusPayload(const char* event, const char* detail) {
  if (!_mqtt.connected()) {
    return false;
  }

  if (!event || !_settings || _settings->mqttStatusTopic.length() == 0) {
    return false;
  }

  StaticJsonDocument<256> doc;
  doc["event"] = event;
  doc["uptimeMs"] = millis();
  doc["pollIntervalMs"] = _pollIntervalMs;
  doc["timeoutRetryMs"] = _timeoutRetryMs;
  doc["loginRetryMs"] = _loginRetryMs;
  if (detail && detail[0] != '\0') {
    doc["detail"] = detail;
  }

  char pubKeyHex[PUB_KEY_SIZE * 2 + 1];
  mesh::Utils::toHex(pubKeyHex, _mesh.self_id.pub_key, PUB_KEY_SIZE);
  JsonObject node = doc.createNestedObject("node");
  node["pubKey"] = pubKeyHex;

  char buffer[256];
  size_t written = serializeJson(doc, buffer, sizeof(buffer));
  if (written == 0 || written >= sizeof(buffer)) {
    RT_INFO_PRINTLN("Failed to serialise status payload for event %s", event ? event : "");
    return false;
  }

  bool published = _mqtt.publish(_settings->mqttStatusTopic.c_str(), buffer, written);
  if (published) {
    RT_INFO_PRINTLN("Published status event %s", event);
  } else {
    RT_INFO_PRINTLN("Failed to publish status event %s", event);
  }
  return published;
}

void RemoteTelemetryManager::publishStatusEvent(const char* event, bool markBoot) {
  bool published = publishStatusPayload(event);
  if (markBoot && published) {
    _statusPublished = true;
  }
}

void RemoteTelemetryManager::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (_instance) {
    _instance->onMqttMessage(topic, payload, length);
  }
}

bool RemoteTelemetryManager::canIssueRequest(unsigned long now) const {
  return _activeRequestType == PendingRequestType::None && now >= _nextRequestReady;
}

void RemoteTelemetryManager::markRequestStarted(PendingRequestType type, size_t index) {
  _activeRequestType = type;
  _activeRequestIndex = index;
}

void RemoteTelemetryManager::markRequestCompleted(PendingRequestType type, size_t index) {
  if (_activeRequestType != type || _activeRequestIndex != index) {
    return;
  }
  _activeRequestType = PendingRequestType::None;
  _activeRequestIndex = INVALID_REQUEST_INDEX;
  _nextRequestReady = millis() + MIN_REQUEST_GAP_MS;
}

void RemoteTelemetryManager::deferNextRequest() {
  unsigned long delayUntil = millis() + MIN_REQUEST_GAP_MS;
  if (delayUntil > _nextRequestReady) {
    _nextRequestReady = delayUntil;
  }
}
