#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#if !defined(ESP32)
#error "Remote telemetry manager requires an ESP32 target with WiFi support"
#endif
#include <helpers/sensors/LPPDataHelpers.h>

#include "RemoteTelemetryMesh.h"
#include "credentials.h"

#ifndef REMOTE_TELEMETRY_DEBUG
#define REMOTE_TELEMETRY_DEBUG 1
#endif

#ifndef REQ_TYPE_GET_TELEMETRY_DATA
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#endif

constexpr size_t REMOTE_TELEMETRY_MAX_REPEATERS = 16;

class RemoteTelemetryManager {
public:
  RemoteTelemetryManager(RemoteTelemetryMesh& mesh, PubSubClient& mqttClient);

  void begin();
  void loop();

  void handleLoginResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len);
  void handleTelemetryResponse(const ContactInfo& contact, uint32_t tag, const uint8_t* payload, uint8_t len);
  void notifySendTimeout();

private:
  struct RepeaterState {
    const RepeaterCredential* creds;
    const ContactInfo* contact;
    bool loginPending;
    bool loggedIn;
    bool telemetryPending;
    uint32_t pendingTelemetryTag;
    unsigned long loginDeadline;
    unsigned long telemetryDeadline;
    unsigned long nextLoginAttempt;
    unsigned long nextTelemetryPoll;
    unsigned long lastLoginSuccess;
    unsigned long lastLoginRequestSent;
    unsigned long lastTelemetryRequestSent;
  };

  RemoteTelemetryMesh& _mesh;
  PubSubClient& _mqtt;
  RepeaterState _repeaters[REMOTE_TELEMETRY_MAX_REPEATERS];
  size_t _repeaterCount;
  bool _debugEnabled;
  unsigned long _bootMillis;
  unsigned long _nextWifiRetry;
  unsigned long _nextMqttRetry;
  unsigned long _loginRetryMs;
  unsigned long _pollIntervalMs;
  unsigned long _timeoutRetryMs;
  bool _statusPublished;
  bool _controlSubscribed;
  unsigned long _nextRequestReady;

  enum class PendingRequestType : uint8_t {
    None,
    Login,
    Telemetry,
  };

  PendingRequestType _activeRequestType;
  size_t _activeRequestIndex;

  void configureRepeaters();
  void ensureWifi();
  void ensureMqtt();
  void processRepeaters();
  void scheduleLogin(RepeaterState& state, unsigned long delayMs);
  bool requestTelemetry(RepeaterState& state, size_t index);
  void publishTelemetry(const RepeaterState& state, uint32_t tag, const uint8_t* payload, uint8_t len);
  int findRepeaterIndex(const ContactInfo& contact) const;
  void checkRebootWindow();
  void onMqttMessage(const char* topic, const uint8_t* payload, size_t length);
  void handleControlMessage(const uint8_t* payload, size_t length);
  bool applyPollInterval(unsigned long intervalMs);
  bool publishStatusPayload(const char* event);
  void publishStatusEvent(const char* event, bool markBoot);
  static void mqttCallback(char* topic, uint8_t* payload, unsigned int length);
  bool canIssueRequest(unsigned long now) const;
  void markRequestStarted(PendingRequestType type, size_t index);
  void markRequestCompleted(PendingRequestType type, size_t index);
  void deferNextRequest();

  static RemoteTelemetryManager* _instance;
};
