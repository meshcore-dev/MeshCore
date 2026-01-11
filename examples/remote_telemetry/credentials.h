#pragma once

#include <cstddef>
#include <stdint.h>

struct WifiCredentials {
	const char* ssid;
	const char* password;
};

struct MqttCredentials {
	const char* host;      // e.g. "mqtt.example.com"
	uint16_t port;         // e.g. 1883
	const char* username;
	const char* password;
	const char* topic;     // base topic for published telemetry
};

struct RepeaterCredential {
	const char* name;      // Friendly label used in debug output and MQTT payloads
	const char* password;  // Repeater admin or guest password required for login
	uint8_t pub_key[32];   // Repeater public key (32 bytes)
};

extern const WifiCredentials WIFI_CREDENTIALS;
extern const MqttCredentials MQTT_CREDENTIALS;
extern const RepeaterCredential REPEATER_CREDENTIALS[];
extern const size_t NUM_REPEATER_CREDENTIALS;
extern const unsigned long REMOTE_TELEMETRY_LOGIN_RETRY_INTERVAL_MS;
extern const unsigned long REMOTE_TELEMETRY_POLL_INTERVAL_MS;
extern const unsigned long REMOTE_TELEMETRY_TIMEOUT_RETRY_INTERVAL_MS;
extern const char* MQTT_STATUS_TOPIC;
extern const char* MQTT_CONTROL_TOPIC;

#ifdef REMOTE_TELEMETRY_REQUIRE_CREDENTIALS
#error "Populate WIFI_CREDENTIALS, MQTT_CREDENTIALS and REPEATER_CREDENTIALS in credentials.cpp or convert this header to contain concrete values before building."
#endif
