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
  DataStore store(InternalFS, rtc_clock);
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
    #ifdef T1000E_BLE_OPTIMIZED
      #include <helpers/nrf52/T1000eBLEInterface.h>
      T1000eBLEInterface serial_interface;
    #elif defined(NANO_G2_ULTRA_BLE_OPTIMIZED)
      #include <helpers/nrf52/NanoG2UltraBLEInterface.h>
      NanoG2UltraBLEInterface serial_interface;
    #elif defined(TECHO_BLE_OPTIMIZED)
      #include <helpers/nrf52/TEchoBLEInterface.h>
      TEchoBLEInterface serial_interface;
    #else
      #include <helpers/nrf52/SerialBLEInterface.h>
      SerialBLEInterface serial_interface;
    #endif
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
  DisplayDriver* disp = NULL;  // Global display pointer
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);
  
  // Add a small delay to ensure serial is ready
  delay(100);
  
  Serial.println("MeshCore Companion Radio Starting...");

  board.begin();

#ifdef DISPLAY_CLASS
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

  if (!radio_init()) { 
    Serial.println("ERROR: Radio initialization failed!");
    halt(); 
  }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef BLE_PIN_CODE
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  Serial.printf("Starting BLE with device name: %s\n", dev_name);
  Serial.printf("BLE PIN Code: %d\n", the_mesh.getBLEPin());
  
  serial_interface.begin(dev_name, the_mesh.getBLEPin());
  
  // Enable BLE debug logging if available
  #ifdef BLE_DEBUG_LOGGING
    Serial.println("BLE debug logging enabled");
  #endif
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
  Serial.printf("Starting WiFi interface on port %d\n", TCP_PORT);
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  Serial.printf("Starting BLE with device name: %s\n", dev_name);
  Serial.printf("BLE PIN Code: %d\n", the_mesh.getBLEPin());
  
  serial_interface.begin(dev_name, the_mesh.getBLEPin());
  
  // Enable BLE debug logging if available
  #ifdef BLE_DEBUG_LOGGING
    Serial.println("BLE debug logging enabled");
  #endif
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

#ifdef BLE_PIN_CODE
  Serial.println("BLE Companion Radio initialized successfully");
  Serial.println("Ready for iOS device connections with enhanced stability");
#else
  Serial.println("Companion Radio initialized successfully");
#endif
}

// BLE connection monitoring variables
unsigned long last_connection_check = 0;
unsigned long last_stats_print = 0;
bool was_connected = false;

void loop() {
  the_mesh.loop();
  sensors.loop();
  
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif

#ifdef BLE_PIN_CODE
  // Monitor BLE connection status periodically
  unsigned long now = millis();
  
  // Check connection status every second
  if (now - last_connection_check >= 1000) {
    bool is_connected = serial_interface.isConnected();
    
    #ifdef T1000E_BLE_OPTIMIZED
      // T1000-E specific battery and GPS monitoring
      uint16_t battery_mv = board.getBattMilliVolts();
      serial_interface.setBatteryVoltage(battery_mv);
      
      // Check if GPS is currently active (simplified check)
      // In a real implementation, this would check actual GPS status
      bool gps_active = false; // TODO: Integrate with actual GPS status
      serial_interface.setGPSActive(gps_active);
    #elif defined(NANO_G2_ULTRA_BLE_OPTIMIZED)
      // Nano G2 Ultra specific battery, display, and GPS monitoring
      uint16_t battery_mv = board.getBattMilliVolts();
      serial_interface.setBatteryVoltage(battery_mv);
      
      // Check if display is currently active (using display object if available)
      #ifdef DISPLAY_CLASS
        bool display_active = (disp != NULL);
        serial_interface.setDisplayActive(display_active);
      #endif
      
      // Check if GPS is currently active (simplified check)
      // In a real implementation, this would check actual GPS status
      bool gps_active = false; // TODO: Integrate with actual GPS status
      serial_interface.setGPSActive(gps_active);
    #elif defined(TECHO_BLE_OPTIMIZED)
      // T-Echo specific battery, e-paper, and GPS monitoring
      uint16_t battery_mv = board.getBattMilliVolts();
      serial_interface.setBatteryVoltage(battery_mv);
      
      // Check if e-paper display is currently refreshing
      // This is a simplified check - in reality would need display driver integration
      bool epaper_refreshing = false; // TODO: Integrate with actual e-paper refresh status
      serial_interface.setEPaperRefreshing(epaper_refreshing);
      
      // Check if GPS is currently active (simplified check)
      // In a real implementation, this would check actual GPS status
      bool gps_active = false; // TODO: Integrate with actual GPS status
      serial_interface.setGPSActive(gps_active);
    #endif
    
    if (is_connected != was_connected) {
      if (is_connected) {
        Serial.println("BLE device connected!");
        
        #ifdef T1000E_BLE_OPTIMIZED
          Serial.printf("T1000-E: Battery level %u mV, Low power mode: %s\n", 
                       board.getBattMilliVolts(),
                       serial_interface.isLowPowerMode() ? "ON" : "OFF");
        #endif
        
        #ifdef DISPLAY_CLASS
        if (disp) {
          disp->startFrame();
          disp->drawTextCentered(disp->width() / 2, 40, "BLE Connected");
          disp->endFrame();
        }
        #endif
      } else {
        Serial.println("BLE device disconnected");
        
        #ifdef DISPLAY_CLASS
        if (disp) {
          disp->startFrame();
          disp->drawTextCentered(disp->width() / 2, 40, "BLE Waiting...");
          disp->endFrame();
        }
        #endif
      }
      was_connected = is_connected;
    }
    
    last_connection_check = now;
  }
  
  // Print connection statistics every 30 seconds if available
  if (now - last_stats_print >= 30000) {
    if (serial_interface.isEnabled()) {
      // Check if the interface has statistics methods (improved versions)
      #ifdef BLE_DEBUG_LOGGING
        // Only call these methods if BLE is actually being used
        #if defined(BLE_PIN_CODE)
          serial_interface.printConnectionStats();
          Serial.printf("Connection stable: %s\n", 
                       serial_interface.isConnectionStable() ? "YES" : "NO");
          
          #ifdef T1000E_BLE_OPTIMIZED
            Serial.printf("T1000-E: Battery %u mV, Power mode: %s\n",
                         board.getBattMilliVolts(),
                         serial_interface.isLowPowerMode() ? "LOW" : "NORMAL");
          #endif
        #endif
      #endif
    }
    last_stats_print = now;
  }
#endif
}
