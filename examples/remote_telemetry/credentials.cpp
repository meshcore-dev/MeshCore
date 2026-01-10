#include "credentials.h"

#ifndef REMOTE_TELEMETRY_CUSTOM_CREDENTIALS
const WifiCredentials WIFI_CREDENTIALS = {
  "<set-ssid>",
  "<set-password>"
};

const MqttCredentials MQTT_CREDENTIALS = {
  "mqtt.example.com",
  1883,
  "<set-username>",
  "<set-password>",
  "meshcore/telemetry"
};

const RepeaterCredential REPEATER_CREDENTIALS[] = {
  // Populate with repeater details, for example:
  // { "Repeater A", "password", { 0x12, 0x34, ... 32 bytes ... } }
};

const size_t NUM_REPEATER_CREDENTIALS = sizeof(REPEATER_CREDENTIALS) / sizeof(REPEATER_CREDENTIALS[0]);
#endif
