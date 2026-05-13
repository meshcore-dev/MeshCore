#pragma once

// Configuration file for WiFi and MQTT settings
// Edit this file before flashing the device

#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H

// ============= WiFi Configuration =============
// Set to your WiFi network credentials
#define WIFI_SSID     "your_wifi_ssid"
#define WIFI_PWD      "your_wifi_password"

// ============= MQTT Configuration =============
// Enable or disable MQTT feature (true/false)
#define MQTT_ENABLED  false

// MQTT Broker settings
#define MQTT_SERVER   "mqtt.example.com"
#define MQTT_PORT     1883

// MQTT Authentication (leave empty if not required)
#define MQTT_USER     ""
#define MQTT_PASSWORD ""

// MQTT Topic prefix (e.g., "meshcore/device123")
// Messages will be published to:
//   - {prefix}/messages/incoming
//   - {prefix}/messages/outgoing
//   - {prefix}/messages/channel
#define MQTT_TOPIC_PREFIX "meshcore/companion_radio"

// ============= Advanced Settings =============
// Debug logging for MQTT (true/false)
#define MQTT_DEBUG_LOGGING false

#endif // MQTT_CONFIG_H
