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

// interface manager
#include <helpers/MultiSerialInterface.h>
MultiSerialInterface interface_manager;

// include bluetooth interface
#if defined(BLE_PIN_CODE)
  #ifdef ESP32
    // include esp32 bluetooth interface
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #elif defined(NRF52_PLATFORM)
    // include nrf52 bluetooth interface
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #else
    #error "SerialBLEInterface is not defined for this platform"
  #endif
#endif

// include wifi interface
#ifdef WIFI_SSID
  #ifndef TCP_PORT
    #define TCP_PORT 5000
  #endif
  #ifdef ESP32
    // include esp32 wifi interface
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface wifi_interface;
  #else
    #error "SerialWifiInterface is not defined for this platform"
  #endif
#endif

// include usb interface
#if defined(ENABLE_USB_INTERFACE)
  #include <helpers/ArduinoSerialInterface.h>
  #if defined(ESP32) && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT \
      && !(defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1)
    #include <tusb.h>    // tud_cdc_n_get_line_state(): real DTR, see setup()
  #endif
  ArduinoSerialInterface usb_serial_interface;
  #ifndef USB_CLIENT_IDLE_TIMEOUT
    // how long a USB client is still considered present after its last frame,
    // for targets which cannot report DTR (see setConnectedCheck below)
    #define USB_CLIENT_IDLE_TIMEOUT   (10*60*1000UL)
  #endif
  #if defined(ESP32) && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 \
      && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // Debounced link state: the HWCDC indication drops out on isolated missed
  // SOF ticks, and acting on a single false sample retires a live session.
  //
  // Confirmation is measured over OBSERVED down samples, not as time since the
  // link was last seen up. Those differ whenever nobody looked in between: a
  // synchronous flash save can block for longer than the whole window, and
  // "last seen up is old" would then make the very first false sample count as
  // a sustained loss. So: start confirming at the first observed down sample,
  // cancel on any observed up, and start over after a sampling gap that is
  // itself longer than the window.
  #define USB_LINK_DOWN_CONFIRM_MS  500
  static bool usb_link_down_pending = false;
  static uint32_t usb_link_down_since = 0;
  static uint32_t usb_link_last_sample_ms = 0;
  static bool usb_link_sampled = false;
  static bool usb_link_up() {
    uint32_t now = millis();
    bool sampled_before = usb_link_sampled;
    uint32_t since_sample = now - usb_link_last_sample_ms;
    usb_link_last_sample_ms = now; usb_link_sampled = true;

    if ((bool)Serial) { usb_link_down_pending = false; return true; }

    // no usable observation history -> this down sample is the first one
    if (!sampled_before || since_sample > USB_LINK_DOWN_CONFIRM_MS) {
      usb_link_down_pending = true; usb_link_down_since = now;
      return true;
    }
    if (!usb_link_down_pending) { usb_link_down_pending = true; usb_link_down_since = now; }

    // A frame that completed AFTER the suspicion started proves the link is
    // alive, whatever the SOF flag says: on resume the RX path can deliver
    // before the SOF tick publishes recovery, and retiring then would erase
    // activity newer than the doubt and drop the reply with it.
    uint32_t last = usb_serial_interface.getLastFrameMillis();
    if (last != 0 && (int32_t)(last - usb_link_down_since) >= 0) {
      usb_link_down_pending = false;
      return true;
    }
    if ((now - usb_link_down_since) <= USB_LINK_DOWN_CONFIRM_MS) return true;

    // Confirmed. Retire the activity mark HERE, not from the outer loop: any
    // caller may be the one that confirms it, and a link that returns before
    // the loop looks again would otherwise cancel the pending loss and leave
    // the next session inheriting eligibility it never earned.
    if (usb_serial_interface.getLastFrameMillis() != 0) {
      usb_serial_interface.resetActivity();
    }
    return false;
  }
  #endif
#endif

// include ethernet interface
#if defined(ETHERNET_ENABLED)
  #include <helpers/ethernet/EthernetInterface.h>
  ETHERNET_CLASS ethernet_interface;
#endif

// include hardware serial interface
#if defined(SERIAL_RX)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface hardware_serial_interface;
  HardwareSerial companion_serial(1);
#endif

// platform file system
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

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &interface_manager);
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

/* WIFI RECONNECT TRACKERS */
#if defined(ESP32) && defined(WIFI_SSID)
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
#endif

void setup() {
#if defined(ENABLE_USB_INTERFACE) && defined(ESP32) \
    && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 \
    && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // BEFORE begin(): setTxBufferSize() frees the old ring without masking the
  // HWCDC interrupt, so resizing a live buffer can hand a TX-empty ISR freed
  // memory. While the ISR is still inactive the same call is harmless.
  Serial.setTxBufferSize(4096);
#endif
  Serial.begin(115200);
  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

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

  fast_rng.begin(radio_driver.getRngSeed());

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
#else
  #error "need to define filesystem"
#endif

// add bluetooth interface
#if defined(BLE_PIN_CODE)
  bluetooth_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
#endif

// add wifi interface
#ifdef WIFI_SSID
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
      if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          WIFI_DEBUG_PRINTLN("WiFi disconnected. Flagging for reconnect...");
          wifi_needs_reconnect = true;
      } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
          wifi_needs_reconnect = false;
      }
  });

  WiFi.begin(WIFI_SSID, WIFI_PWD);
  wifi_interface.begin(TCP_PORT);
  interface_manager.addInterface(InterfaceType::WiFi, &wifi_interface);
#endif

// add usb interface
#if defined(ENABLE_USB_INTERFACE)
  usb_serial_interface.begin(Serial);
  // NOTE: flow control is enabled per transport below, NOT unconditionally.
  // It must only run on native CDC links whose TX buffer can hold a whole
  // frame; on a UART-backed Serial (no CDC-on-boot) it would pace and drop
  // against a buffer whose size the sketch may change, while isConnected()
  // there has no link state to go by and always answers "connected".
#if defined(ESP32) && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 \
    && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // ESP32 USB-Serial-JTAG (HWCDC): has NO DTR concept -- (bool)Serial is true
  // as soon as the host has merely enumerated the device (SOF/IN-EMPTY), and
  // write() blocks up to 100ms per call against a stalled host with only a
  // 256 byte TX buffer. Bigger buffer + short timeout + activity-based
  // connection detection (a real client must have sent a frame recently).
  // whole-frame writes + pacing of the contact sync stream: prevents torn
  // frames / blocking writes when the host stalls or nothing drains the port
  usb_serial_interface.enableFlowControl(true);
  Serial.setTxTimeoutMs(5);   // buffer was sized before begin(), see setup()
  usb_serial_interface.setConnectedCheck([]() {
    uint32_t last = usb_serial_interface.getLastFrameMillis();
    return usb_link_up() && last != 0 && (millis() - last) < USB_CLIENT_IDLE_TIMEOUT;
  });
  // Push notifications must survive a client that only listens: the ten minute
  // window above exists to stop a merely enumerated port (a charger) from
  // looking connected, and once a real frame has arrived that question is
  // settled. resetActivity() clears _last_frame_ms on link-down, so this does
  // not resurrect a ghost session.
  usb_serial_interface.setEstablishedCheck([]() {
    return usb_link_up() && usb_serial_interface.getLastFrameMillis() != 0;
  });
#elif defined(ESP32) && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // ESP32 USB-OTG (TinyUSB CDC): USBCDC::operator bool() is NOT a DTR test --
  // it only turns true once the host asserts DTR *and* RTS. Clients that leave
  // RTS low on purpose (the usual way to keep the auto-reset circuit on these
  // boards quiet) can exchange data perfectly well, and would have every reply
  // silently dropped. Ask TinyUSB for the line state instead: bit 0 is DTR.
  // Queried live rather than latched from the line-state event, because USB is
  // up before setup() runs -- a host that asserts DTR before we could register
  // a handler would never be seen, and USBCDC suppresses duplicate events.
  usb_serial_interface.enableFlowControl(true);
  usb_serial_interface.setConnectedCheck([]() {
    return (tud_cdc_n_get_line_state(0) & 1) != 0;   // bit 0 = DTR
  });
#elif defined(NRF52_PLATFORM) || defined(RP2040_PLATFORM)
  // TinyUSB-CDC: (bool)Serial reflects real DTR (host has the port open); a
  // false "always connected" would hide the BLE pairing PIN on the display
  // and suppress new-message notifications.
  // (classic ESP32 with a UART bridge keeps the old assume-connected behaviour
  //  AND stays without flow control -- see the note above)
  usb_serial_interface.enableFlowControl(true);
  usb_serial_interface.setConnectedCheck([]() { return (bool)Serial; });
#endif
  interface_manager.addInterface(InterfaceType::USB, &usb_serial_interface);
#endif

// add ethernet interface
#if defined(ETHERNET_ENABLED)
  ethernet_interface.begin();
  interface_manager.addInterface(InterfaceType::Ethernet, &ethernet_interface);
#endif

// add hardware serial interface
#if defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  hardware_serial_interface.begin(companion_serial);
  interface_manager.addInterface(InterfaceType::HardwareSerial, &hardware_serial_interface);
#endif

  the_mesh.startInterface(interface_manager);
  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif

  board.onBootComplete();
}

void loop() {
#if defined(ENABLE_USB_INTERFACE) && defined(ESP32) \
    && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 \
    && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // HWCDC: client activity must not survive the USB session it happened in.
  // (bool)Serial goes true on mere enumeration, so without this a re-plug
  // within the 10min activity window counts as "client connected" before the
  // new session has sent a single frame -- mirrored traffic then fills the TX
  // Poll the link so a loss is noticed even when nothing else asks: the
  // retirement itself happens inside usb_link_up(), at the moment the loss is
  // confirmed. Nothing is drained and the parser is not touched. Deliberate
  // trade-off: a host SUSPEND reads as link-down too (SOF loss is
  // indistinguishable from an unplug without DTR), so after a resume the
  // client sends one frame before paced mirroring restarts -- notifications
  // are unaffected, they use the session predicate.
  (void)usb_link_up();
#endif
  the_mesh.loop();
  interface_manager.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif

  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#endif
  }

#if defined(ESP32) && defined(WIFI_SSID)
  // Safely attempt to reconnect every 10 seconds if flagged
  if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > 10000)) {
    WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    last_wifi_reconnect_attempt = millis();
  }
#endif
}
