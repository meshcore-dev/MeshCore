#ifdef ESP_PLATFORM

#include "ESP32Board.h"

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA)   // Repeater or Room Server only
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

#include <SPIFFS.h>

// Workaround for missing loop() in Board class
// It is less ugly than change API of whole
// project just because of one OTA reboot routine
// used probably once per month
static void otaRebootTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
}

bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  inhibit_sleep = true;   // prevent sleep during OTA
  WiFi.softAP("MeshCore-OTA", NULL);

  sprintf(reply, "Started: http://%s/update", WiFi.softAPIP().toString().c_str());
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);

  static char id_buf[60];
  sprintf(id_buf, "%s (%s)", id, getManufacturerName());
  static char home_buf[90];
  sprintf(home_buf, "<H2>Hi! I am a MeshCore Repeater. ID: %s</H2>", id);

  AsyncWebServer* server = new AsyncWebServer(80);

  server->on("/", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
  });
  server->on("/log", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  ElegantOTA.begin(server);    // Start ElegantOTA
  ElegantOTA.setAutoReboot(false);
  ElegantOTA.onEnd([](bool success) {
    if (success) {
      // Can not use ESP.restart() here, because
      // this callback is called before HTTP response
      // so we need create separated task (thread)
      xTaskCreate(otaRebootTask, "ota-reboot", 2048, NULL, 1, NULL);
    }
  });
  server->begin();

  return true;
}

#else
bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  return false; // not supported
}
#endif

#endif
