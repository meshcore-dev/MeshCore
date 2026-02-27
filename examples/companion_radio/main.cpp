#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

#ifdef WIFI_SSID
static char _wifi_ssid[33];
static char _wifi_pwd[65];
#endif

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);

  board.begin();

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef BLE_PIN_CODE
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef WIFI_SSID
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active

  // Load credentials from flash if provisioned via CLI rescue; fall back to compiled-in defaults
  strncpy(_wifi_ssid, WIFI_SSID, sizeof(_wifi_ssid) - 1);
  strncpy(_wifi_pwd,  WIFI_PWD,  sizeof(_wifi_pwd)  - 1);
  {
    File wf = SPIFFS.open("/wifi_config", "r");
    if (wf) {
      int n = wf.readBytesUntil('\n', _wifi_ssid, sizeof(_wifi_ssid) - 1);
      _wifi_ssid[n] = 0;
      n = wf.readBytesUntil('\n', _wifi_pwd, sizeof(_wifi_pwd) - 1);
      _wifi_pwd[n] = 0;
      wf.close();
      WIFI_DEBUG_PRINTLN("Loaded credentials from flash, SSID: %s", _wifi_ssid);
    } else {
      WIFI_DEBUG_PRINTLN("No /wifi_config found, using compiled-in SSID: %s", _wifi_ssid);
    }
  }

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_START:
        WIFI_DEBUG_PRINTLN("STA started, connecting to SSID: %s", _wifi_ssid);
        break;
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        WIFI_DEBUG_PRINTLN("Associated with AP: %s, channel: %d",
          (char*)info.wifi_sta_connected.ssid,
          info.wifi_sta_connected.channel);
        break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        WIFI_DEBUG_PRINTLN("Got IP: %s", WiFi.localIP().toString().c_str());
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        WIFI_DEBUG_PRINTLN("Disconnected, reason: %d",
          info.wifi_sta_disconnected.reason);
        break;
      case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
        WIFI_DEBUG_PRINTLN("Auth mode changed");
        break;
      case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        WIFI_DEBUG_PRINTLN("Lost IP");
        break;
      default:
        break;
    }
  });
  WiFi.persistent(false);        // don't use/overwrite NVS-cached credentials
  WiFi.mode(WIFI_STA);
  WiFi.begin(_wifi_ssid, _wifi_pwd);
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  sensors.begin();

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif
}

#ifdef defined(WIFI_SSID) && defined(ESP32)
static void wifi_reconnect_check() {
  constexpr uint32_t CHECK_INTERVAL_MS  = 5000;
  constexpr uint32_t RECONNECT_AFTER_MS = 30000;

  static uint32_t last_check   = 0;
  static uint32_t down_since   = 0;

  if (millis() - last_check < CHECK_INTERVAL_MS) return;
  last_check = millis();

  if (WiFi.status() == WL_CONNECTED) {
    down_since = 0;
    return;
  }

  if (down_since == 0) {
    down_since = millis();
    WIFI_DEBUG_PRINTLN("WiFi disconnected, waiting to reconnect...");
    return;
  }

  if (millis() - down_since >= RECONNECT_AFTER_MS) {
    WIFI_DEBUG_PRINTLN("WiFi reconnecting...");
    WiFi.disconnect(true);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_wifi_ssid, _wifi_pwd);
    down_since = 0;
  }
}
#endif

void loop() {
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#ifdef WIFI_SSID
  wifi_reconnect_check();
#endif
}
