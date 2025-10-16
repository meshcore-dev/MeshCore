#ifdef ESP_PLATFORM

#include "ESP32Board.h"

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA)   // Repeater or Room Server only
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SPIFFS.h>

static void htmlEscape(char* dest, size_t dest_sz, const char* src) {
  if (dest_sz == 0) return;
  size_t i = 0;
  for (; *src && i + 1 < dest_sz; ++src) {
    const char* rep = nullptr;
    switch (*src) {
      case '&': rep = "&amp;"; break;
      case '<': rep = "&lt;";  break;
      case '>': rep = "&gt;";  break;
      case '"': rep = "&quot;"; break;
      case 39: rep = "&#39;"; break; // '\''
      default: rep = nullptr; break;
    }
    if (rep) {
      for (const char* p = rep; *p && i + 1 < dest_sz; ++p) dest[i++] = *p;
    } else {
      dest[i++] = *src;
    }
  }
  dest[i] = 0;
}

bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  WiFi.softAP("MeshCore-OTA", NULL);

  sprintf(reply, "Started: http://%s/update", WiFi.softAPIP().toString().c_str());
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);

  // HTML-escape dynamic values to avoid breaking the page with special chars
  static char id_safe[128];
  static char man_safe[64];
  htmlEscape(id_safe, sizeof(id_safe), id);
  htmlEscape(man_safe, sizeof(man_safe), getManufacturerName());

  static char id_buf[200];
  snprintf(id_buf, sizeof(id_buf), "%s (%s)", id_safe, man_safe);
  static char home_buf[240];
  snprintf(home_buf, sizeof(home_buf), "<H2>Hi! I am a MeshCore Repeater. ID: %s</H2>", id_safe);

  AsyncWebServer* server = new AsyncWebServer(80);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
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
