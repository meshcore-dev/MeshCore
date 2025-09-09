#ifdef ESP_PLATFORM

#include "ESP32Board.h"

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA)   // Repeater or Room Server only
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SPIFFS.h>

bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  WiFi.softAP("MeshCore-OTA", NULL);

  sprintf(reply, "Started: http://%s/update", WiFi.softAPIP().toString().c_str());
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
  AsyncElegantOTA.begin(server);    // Start ElegantOTA
  server->begin();

  return true;
}

WiFiEvent_t last_event = ARDUINO_EVENT_MAX;

void WiFiEvent(WiFiEvent_t event){

  if(last_event != event){
    Serial.printf("WiFi event %i\n\r", event);
  }

  switch(event){
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      WiFi.enableIpV6();
      break;
    default:
      break;
  }

  last_event = event;
}

bool ESP32Board::startWiFi(char* ssid, char* password, bool apMode){

  WiFi.onEvent(WiFiEvent);

  if(apMode){
    
    WiFi.mode(WIFI_MODE_AP);
    WiFi.disconnect(true);
    WiFi.softAPenableIpV6();
    WiFi.softAP(ssid, password);
  
  } else {

    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(true);
    WiFi.begin(ssid, password);

  }

  return true;
}

#else
bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  return false; // not supported
}

bool ESP32Board::startWiFi(char* ssid, char* password, bool apMode){
  return false; //not supported
}
#endif

#endif
