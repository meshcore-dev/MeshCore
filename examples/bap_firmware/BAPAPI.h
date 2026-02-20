#pragma once

#include <Arduino.h>
#ifdef ESP32
  #include <WiFi.h>
  #include <HTTPClient.h>
#endif
#include "MyMesh.h"

// API polling interval in milliseconds
#ifndef BAP_API_POLL_INTERVAL
#define BAP_API_POLL_INTERVAL 60000  // 60 seconds default
#endif

// Request timeout in milliseconds
#ifndef BAP_API_TIMEOUT
#define BAP_API_TIMEOUT 10000  // 10 seconds
#endif

// Maximum arrivals to fetch per request
#define BAP_MAX_ARRIVALS 20

class BAPAPIClient {
public:
  BAPAPIClient();

  // WiFi connection management
  bool connectWiFi(const char* ssid, const char* password);
  void disconnectWiFi();
  bool isWiFiConnected();

  // API configuration
  void setApiKey(const char* api_key);
  void setApiEndpoint(const char* endpoint);
  void setPollInterval(uint32_t interval_ms);

  // Fetch arrivals for a stop (or multiple stops)
  // Returns number of arrivals fetched, -1 on error
  int fetchArrivals(uint32_t stop_id, BusArrival* arrivals, int max_arrivals);

  // Fetch arrivals for all stops in the network (gateway mode)
  // This fetches multiple stops and broadcasts all arrivals
  int fetchAllArrivals(BusArrival* arrivals, int max_arrivals);

  // Check if it's time to poll again
  bool shouldPoll();

  // Mark that we just polled
  void markPolled();

  // Get time since last successful poll
  uint32_t getTimeSinceLastPoll();

  // Get last error message
  const char* getLastError() { return _last_error; }

private:
  char _api_key[64];
  char _api_endpoint[128];
  uint32_t _poll_interval;
  unsigned long _last_poll_time;
  char _last_error[128];

  // Parse 511.org SIRI StopMonitoring response
  int parseStopMonitoringResponse(const String& json, BusArrival* arrivals, int max_arrivals, uint32_t filter_stop_id = 0);

  // Parse ISO8601 timestamp to Unix time
  uint32_t parseISO8601(const char* timestamp);

  // Get agency ID from operator code
  uint8_t getAgencyId(const char* operator_ref);

  // URL encode a string
  String urlEncode(const char* str);
};
