#ifdef ESP_PLATFORM

#include "ESP32Board.h"

#if (defined(ADMIN_PASSWORD) || defined(ENABLE_COMPANION_WIFI_OTA)) && !defined(DISABLE_WIFI_OTA)
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SPIFFS.h>

bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  inhibit_sleep = true;   // prevent sleep during OTA
  WiFi.persistent(false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_AP);

  bool ap_ok = WiFi.softAP("MeshCore-OTA", NULL);
  MESH_DEBUG_PRINTLN("startOTAUpdate: softAP returned %d", ap_ok);
  if (!ap_ok) {
    strcpy(reply, "Failed to start WiFi AP");
    return false;
  }

  // Give the AP stack a moment to settle before starting the web server.
  delay(250);

  IPAddress ap_ip = WiFi.softAPIP();
  MESH_DEBUG_PRINTLN("startOTAUpdate: softAPIP=%s", ap_ip.toString().c_str());
  sprintf(reply, "Started: http://%s/update", ap_ip.toString().c_str());
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);

  static char id_buf[60];
  sprintf(id_buf, "%s (%s)", id, getManufacturerName());
  static char home_buf[90];
  sprintf(home_buf, "<H2>Hi! I am a MeshCore node. ID: %s</H2>", id);

  AsyncWebServer* server = new AsyncWebServer(80);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
  });
  server->on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK");
  });
  server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  AsyncElegantOTA.setID(id_buf);
  AsyncElegantOTA.begin(server);    // Start ElegantOTA
  server->begin();

  return true;
}

#else
bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  return false; // not supported
}
#endif

#endif
