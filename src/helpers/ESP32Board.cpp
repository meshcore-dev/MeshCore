#ifdef ESP_PLATFORM

#include "ESP32Board.h"

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA)   // Repeater or Room Server only
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <SPIFFS.h>

#if defined(USE_ETHERNET)
  extern String eth_local_ip;  // defined in TEthEliteBoard.cpp
#else
  #include <WiFi.h>
#endif

bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  inhibit_sleep = true;

#if defined(USE_ETHERNET)
  Serial.println("OTA: using ETH");
  sprintf(reply, "Started: http://%s/update", eth_local_ip.c_str());
#else
  Serial.println("OTA: using WiFi SoftAP");
  WiFi.softAP("MeshCore-OTA", NULL);
  sprintf(reply, "Started: http://%s/update", WiFi.softAPIP().toString().c_str());
#endif

  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);

  static char id_buf[60];
  sprintf(id_buf, "%s (%s)", id, getManufacturerName());
  static char home_buf[90];
  sprintf(home_buf, "<H2>Hi! I am a MeshCore Repeater. ID: %s</H2>", id);

  AsyncWebServer* server = new AsyncWebServer(80);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
  });
  server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  AsyncElegantOTA.setID(id_buf);
  AsyncElegantOTA.begin(server);
  server->begin();

  return true;
}

#else
bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  return false; // not supported
}
#endif

#endif  // ESP_PLATFORM
