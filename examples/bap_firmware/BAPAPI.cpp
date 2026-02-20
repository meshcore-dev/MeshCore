#include "BAPAPI.h"
#include <ArduinoJson.h>

BAPAPIClient::BAPAPIClient()
  : _poll_interval(BAP_API_POLL_INTERVAL), _last_poll_time(0) {
  strcpy(_api_key, "");
  strcpy(_api_endpoint, "http://api.511.org/transit/StopMonitoring");
  strcpy(_last_error, "");
}

bool BAPAPIClient::connectWiFi(const char* ssid, const char* password) {
  MESH_DEBUG_PRINTLN("Connecting to WiFi: %s", ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
    MESH_DEBUG_PRINT(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    MESH_DEBUG_PRINTLN("\nWiFi connected! IP: %s", WiFi.localIP().toString().c_str());
    return true;
  } else {
    strcpy(_last_error, "WiFi connection failed");
    MESH_DEBUG_PRINTLN("\nWiFi connection failed");
    return false;
  }
}

void BAPAPIClient::disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool BAPAPIClient::isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void BAPAPIClient::setApiKey(const char* api_key) {
  strncpy(_api_key, api_key, sizeof(_api_key) - 1);
  _api_key[sizeof(_api_key) - 1] = '\0';
}

void BAPAPIClient::setApiEndpoint(const char* endpoint) {
  strncpy(_api_endpoint, endpoint, sizeof(_api_endpoint) - 1);
  _api_endpoint[sizeof(_api_endpoint) - 1] = '\0';
}

void BAPAPIClient::setPollInterval(uint32_t interval_ms) {
  _poll_interval = interval_ms;
}

bool BAPAPIClient::shouldPoll() {
  if (_last_poll_time == 0) return true;
  return (millis() - _last_poll_time) >= _poll_interval;
}

void BAPAPIClient::markPolled() {
  _last_poll_time = millis();
}

uint32_t BAPAPIClient::getTimeSinceLastPoll() {
  if (_last_poll_time == 0) return UINT32_MAX;
  return millis() - _last_poll_time;
}

int BAPAPIClient::fetchArrivals(uint32_t stop_id, BusArrival* arrivals, int max_arrivals) {
  if (!isWiFiConnected()) {
    strcpy(_last_error, "WiFi not connected");
    return -1;
  }

  if (_api_key[0] == '\0') {
    strcpy(_last_error, "API key not set");
    return -1;
  }

  // Build path and query string
  String path = String("/transit/StopMonitoring") +
                "?api_key=" + urlEncode(_api_key) +
                "&agency=SF" +
                "&stopCode=" + String(stop_id);

  MESH_DEBUG_PRINTLN("Fetching path: %s", path.c_str());

  // Use WiFiClient directly for more control
  WiFiClient client;
  client.setTimeout(BAP_API_TIMEOUT / 1000);  // setTimeout takes seconds

  if (!client.connect("api.511.org", 80)) {
    strcpy(_last_error, "Failed to connect to API server");
    return -1;
  }

  // Send HTTP request with headers to prevent compression
  client.print(String("GET ") + path + " HTTP/1.1\r\n");
  client.print("Host: api.511.org\r\n");
  client.print("Accept: application/json\r\n");
  client.print("Accept-Encoding: identity\r\n");
  client.print("Connection: close\r\n");
  client.print("\r\n");

  // Wait for response
  unsigned long start = millis();
  while (!client.available() && millis() - start < BAP_API_TIMEOUT) {
    delay(10);
  }

  // Read response headers
  String response;
  bool headers_done = false;
  int content_length = -1;
  bool chunked = false;

  while (client.available()) {
    String line = client.readStringUntil('\n');

    if (!headers_done) {
      // Check for end of headers
      if (line == "\r" || line.length() == 0) {
        headers_done = true;
        continue;
      }
      // Check for Content-Length
      if (line.startsWith("Content-Length:")) {
        content_length = line.substring(15).toInt();
      }
      // Check for chunked encoding
      if (line.startsWith("Transfer-Encoding: chunked")) {
        chunked = true;
      }
      continue;
    }

    // Read body
    if (chunked) {
      // Skip chunk size lines
      line.trim();
      if (line.length() == 0) continue;
      int chunk_size = strtol(line.c_str(), NULL, 16);
      if (chunk_size == 0) break;
      // Read chunk data
      while (client.available() && response.length() < content_length + 16000) {
        char c = client.read();
        response += c;
      }
    } else {
      response += line;
    }
  }

  client.stop();

  if (response.length() == 0) {
    // Try reading everything as body if header parsing failed
    response = client.readString();
  }

  MESH_DEBUG_PRINTLN("Response length: %d bytes", response.length());

  // Parse JSON response
  int count = parseStopMonitoringResponse(response, arrivals, max_arrivals, stop_id);

  if (count < 0) {
    return -1;
  }

  markPolled();
  return count;
}

int BAPAPIClient::fetchAllArrivals(BusArrival* arrivals, int max_arrivals) {
  // For gateway mode, this would fetch arrivals for all known stops
  // Currently a placeholder - in practice, gateway would have a list of stops
  // or would fetch from a custom backend that aggregates multiple stops

  // For now, this just returns an error indicating the gateway should
  // use fetchArrivals() with specific stop IDs
  strcpy(_last_error, "Use fetchArrivals() with specific stop_id");
  return -1;
}

int BAPAPIClient::parseStopMonitoringResponse(const String& json, BusArrival* arrivals, int max_arrivals, uint32_t filter_stop_id) {
  // Use ArduinoJson to parse the SIRI StopMonitoring response
  // The response format is:
  // {
  //   "Siri": {
  //     "ServiceDelivery": {
  //       "StopMonitoringDelivery": [{
  //         "MonitoredStopVisit": [{
  //           "MonitoredVehicleJourney": {...}
  //         }]
  //       }]
  //     }
  //   }
  // }

  // Debug: print first 500 chars of response
  MESH_DEBUG_PRINTLN("API Response (first 500 chars):");
  Serial.println(json.substring(0, 500));

  // 511.org returns UTF-8 BOM at the start - need to skip it
  const char* jsonStr = json.c_str();
  int skip = 0;
  if (json.length() >= 3 && (unsigned char)jsonStr[0] == 0xEF &&
      (unsigned char)jsonStr[1] == 0xBB && (unsigned char)jsonStr[2] == 0xBF) {
    skip = 3;  // Skip UTF-8 BOM
    MESH_DEBUG_PRINTLN("Detected UTF-8 BOM, skipping");
  }

  // Allocate a large enough document for 511.org responses
  // 511.org responses can be quite large - use 16KB to be safe
  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, jsonStr + skip);

  if (error) {
    snprintf(_last_error, sizeof(_last_error), "JSON parse error: %s", error.c_str());
    MESH_DEBUG_PRINTLN("JSON parse failed: %s", error.c_str());
    return -1;
  }

  MESH_DEBUG_PRINTLN("JSON parsed successfully, doc size: %d bytes", doc.memoryUsage());

  int count = 0;

  // Navigate to StopMonitoringDelivery
  // 511.org can return either {"Siri":{"ServiceDelivery":{...}}} or {"ServiceDelivery":{...}}
  JsonObject delivery;

  JsonObject siri = doc["Siri"];
  if (!siri.isNull()) {
    // Format: {"Siri":{"ServiceDelivery":{...}}}
    delivery = siri["ServiceDelivery"];
  } else {
    // Format: {"ServiceDelivery":{...}}
    delivery = doc["ServiceDelivery"];
  }

  if (delivery.isNull()) {
    strcpy(_last_error, "No ServiceDelivery in response");
    return -1;
  }

  JsonArray stopMonitoring = delivery["StopMonitoringDelivery"];
  if (stopMonitoring.isNull()) {
    // Try as single object instead of array
    JsonObject singleDelivery = delivery["StopMonitoringDelivery"];
    if (!singleDelivery.isNull()) {
      // Handle single delivery object
      JsonArray visits = singleDelivery["MonitoredStopVisit"];
      if (!visits.isNull()) {
        for (JsonObject visit : visits) {
          if (count >= max_arrivals) break;

          JsonObject journey = visit["MonitoredVehicleJourney"];
          if (journey.isNull()) continue;

          JsonObject monitoredCall = journey["MonitoredCall"];
          if (monitoredCall.isNull()) continue;

          // Get stop ID and filter if needed
          const char* stopPointRef = monitoredCall["StopPointRef"];
          uint32_t arrival_stop_id = stopPointRef ? atol(stopPointRef) : 0;
          if (filter_stop_id != 0 && arrival_stop_id != filter_stop_id) continue;

          // Fill in arrival data
          BusArrival& arr = arrivals[count];
          memset(&arr, 0, sizeof(arr));

          arr.stop_id = arrival_stop_id;

          const char* lineRef = journey["LineRef"];
          if (lineRef) {
            strncpy(arr.route, lineRef, sizeof(arr.route) - 1);
          }

          const char* destName = journey["DestinationName"];
          if (destName) {
            strncpy(arr.destination, destName, sizeof(arr.destination) - 1);
          }

          const char* operatorRef = journey["OperatorRef"];
          arr.agency_id = getAgencyId(operatorRef ? operatorRef : "SF");

          // Calculate minutes until arrival
          const char* expectedArrival = monitoredCall["ExpectedArrivalTime"];
          if (expectedArrival) {
            uint32_t arrival_time = parseISO8601(expectedArrival);
            uint32_t now = time(nullptr);  // Requires system time to be set
            if (arrival_time > now) {
              arr.minutes = (int16_t)((arrival_time - now) / 60);
            } else {
              arr.minutes = 0;  // Arriving now
            }
          } else {
            arr.minutes = ARRIVAL_MINUTES_NA;
          }

          arr.timestamp = time(nullptr);
          arr.status = ARRIVAL_STATUS_ON_TIME;

          // Check for progress status
          const char* progressStatus = journey["ProgressStatus"];
          if (progressStatus && strstr(progressStatus, "delay") != nullptr) {
            arr.status = ARRIVAL_STATUS_DELAYED;
          }

          count++;
        }
      }
    }
    return count;
  }

  // Handle array of deliveries
  for (JsonObject deliveryObj : stopMonitoring) {
    JsonArray visits = deliveryObj["MonitoredStopVisit"];
    if (visits.isNull()) continue;

    for (JsonObject visit : visits) {
      if (count >= max_arrivals) break;

      JsonObject journey = visit["MonitoredVehicleJourney"];
      if (journey.isNull()) continue;

      JsonObject monitoredCall = journey["MonitoredCall"];
      if (monitoredCall.isNull()) continue;

      // Get stop ID and filter if needed
      const char* stopPointRef = monitoredCall["StopPointRef"];
      uint32_t arrival_stop_id = stopPointRef ? atol(stopPointRef) : 0;
      if (filter_stop_id != 0 && arrival_stop_id != filter_stop_id) continue;

      // Fill in arrival data
      BusArrival& arr = arrivals[count];
      memset(&arr, 0, sizeof(arr));

      arr.stop_id = arrival_stop_id;

      const char* lineRef = journey["LineRef"];
      if (lineRef) {
        strncpy(arr.route, lineRef, sizeof(arr.route) - 1);
      }

      const char* destName = journey["DestinationName"];
      if (destName) {
        strncpy(arr.destination, destName, sizeof(arr.destination) - 1);
      }

      const char* operatorRef = journey["OperatorRef"];
      arr.agency_id = getAgencyId(operatorRef ? operatorRef : "SF");

      // Calculate minutes until arrival
      const char* expectedArrival = monitoredCall["ExpectedArrivalTime"];
      if (expectedArrival) {
        uint32_t arrival_time = parseISO8601(expectedArrival);
        uint32_t now = time(nullptr);
        if (arrival_time > now) {
          arr.minutes = (int16_t)((arrival_time - now) / 60);
        } else {
          arr.minutes = 0;
        }
      } else {
        arr.minutes = ARRIVAL_MINUTES_NA;
      }

      arr.timestamp = time(nullptr);
      arr.status = ARRIVAL_STATUS_ON_TIME;

      count++;
    }
  }

  return count;
}

uint32_t BAPAPIClient::parseISO8601(const char* timestamp) {
  // Parse ISO8601 format: 2024-01-15T10:42:00-08:00
  // This is a simplified parser - production code should use a proper library

  struct tm tm;
  memset(&tm, 0, sizeof(tm));

  int year, month, day, hour, minute, second;
  int tz_hour = 0, tz_min = 0;
  char tz_sign = '+';

  if (sscanf(timestamp, "%d-%d-%dT%d:%d:%d%c%d:%d",
             &year, &month, &day, &hour, &minute, &second, &tz_sign, &tz_hour, &tz_min) >= 6) {
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;

    time_t t = mktime(&tm);

    // Adjust for timezone offset
    int tz_offset = tz_hour * 3600 + tz_min * 60;
    if (tz_sign == '-') {
      t += tz_offset;  // Convert to UTC
    } else {
      t -= tz_offset;
    }

    return (uint32_t)t;
  }

  return 0;
}

uint8_t BAPAPIClient::getAgencyId(const char* operator_ref) {
  if (!operator_ref) return AGENCY_SF_MUNI;

  if (strcmp(operator_ref, "SF") == 0) return AGENCY_SF_MUNI;
  if (strcmp(operator_ref, "AC") == 0) return AGENCY_AC_TRANSIT;
  if (strcmp(operator_ref, "BA") == 0) return AGENCY_BART;
  if (strcmp(operator_ref, "CT") == 0) return AGENCY_CALTRAIN;
  if (strcmp(operator_ref, "GG") == 0) return AGENCY_GGT;
  if (strcmp(operator_ref, "SM") == 0) return AGENCY_SAMTRANS;
  if (strcmp(operator_ref, "VT") == 0) return AGENCY_VTA;

  return 0;  // Unknown agency
}

String BAPAPIClient::urlEncode(const char* str) {
  String encoded;
  while (*str) {
    char c = *str++;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += "0123456789ABCDEF"[c >> 4];
      encoded += "0123456789ABCDEF"[c & 0xF];
    }
  }
  return encoded;
}
