#include <Arduino.h>
#include "target.h"

M5CardputerBoard board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi, SPISettings());

// RF switch control pins (only for modules with external RF switch like DX-LR30)
// Support both old (P_LORA_*) and new (SX126X_*) naming conventions
#if defined(SX126X_RXEN)
  static const int RXEN_PIN = SX126X_RXEN;
#elif defined(P_LORA_RXEN)
  static const int RXEN_PIN = P_LORA_RXEN;
#else
  static const int RXEN_PIN = -1;  // Not used
#endif

#if defined(SX126X_TXEN)
  static const int TXEN_PIN = SX126X_TXEN;
#elif defined(P_LORA_TXEN)
  static const int TXEN_PIN = P_LORA_TXEN;
#else
  static const int TXEN_PIN = -1;  // Not used
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#ifdef HAS_GPS
static MicroNMEALocationProvider location_provider(Serial1, &rtc_clock);
CardputerSensorManager sensors(location_provider);

void CardputerSensorManager::start_gps() {
  Serial.println("[GPS] Starting GPS...");
  Serial.printf("[GPS] Using pins: RX=%d (from GPS-TX), TX=%d (to GPS-RX)\n", GPS_RX_PIN, GPS_TX_PIN);
  MESH_DEBUG_PRINTLN("Starting GPS on pins RX=%d, TX=%d", GPS_RX_PIN, GPS_TX_PIN);
  if (!gps_active) {
    Serial1.setPins(GPS_RX_PIN, GPS_TX_PIN);
    Serial1.begin(115200, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("[GPS] Serial1 configured at 115200 baud (ATGM336H-6N)");
    delay(1000);
    gps_active = true;
    Serial.println("[GPS] GPS marked as active");
    
    // Check if we're receiving any data
    delay(500);
    if (Serial1.available()) {
      Serial.printf("[GPS] Receiving data! Available bytes: %d\n", Serial1.available());
    } else {
      Serial.println("[GPS] WARNING: No data from GPS yet");
    }
  }
  _location->begin();
  Serial.println("[GPS] Location provider initialized");
}

void CardputerSensorManager::stop_gps() {
  Serial.println("[GPS] Stopping GPS");
  MESH_DEBUG_PRINTLN("Stopping GPS");
  if (gps_active) {
    gps_active = false;
  }
  _location->stop();
}

bool CardputerSensorManager::begin() {
  Serial.println("[GPS] CardputerSensorManager::begin() - GPS support available");
  MESH_DEBUG_PRINTLN("CardputerSensorManager::begin()");
  
  // Check if GPS was enabled before (restore state from NodePrefs)
  // NodePrefs will be set later via setNodePrefs(), so we can't check it here yet
  Serial.println("[GPS] GPS is OFF by default. Will be restored from NodePrefs after initialization.");
  
  return true;
}

bool CardputerSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void CardputerSensorManager::loop() {
  static long next_gps_update = 0;
  static bool first_run = true;

  if (!gps_active) {
    if (first_run) {
      first_run = false;
    }
    return;
  }

  if (first_run) {
    Serial.println("[GPS] GPS loop started - GPS is ACTIVE");
    first_run = false;
  }

  _location->loop();

  if (millis() > next_gps_update) {
    Serial.println("[GPS] Update - checking status...");
    Serial.printf("[GPS] Valid: %d, Satellites: %d\n", _location->isValid(), _location->satellitesCount());
    
    if (_location->isValid()) {
      node_lat = ((double)_location->getLatitude()) / 1000000.;
      node_lon = ((double)_location->getLongitude()) / 1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      Serial.printf("[GPS] Location VALID: lat %.6f lon %.6f alt %.1fm\n", node_lat, node_lon, node_altitude);
    } else {
      Serial.println("[GPS] Location INVALID - waiting for fix...");
    }
    
    next_gps_update = millis() + 180000;  // update every 3 minutes
  }
}

int CardputerSensorManager::getNumSettings() const {
  return 1;
}

const char* CardputerSensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}

const char* CardputerSensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}

bool CardputerSensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    bool should_enable = (strcmp(value, "0") != 0);
    Serial.printf("[GPS] App requested GPS %s\n", should_enable ? "ON" : "OFF");
    
    if (should_enable) {
      start_gps();
    } else {
      stop_gps();
    }
    
    // Sync with NodePrefs
    if (_node_prefs) {
      struct NodePrefs { 
        uint8_t airtime_factor;
        char node_name[32];
        float freq; uint8_t sf; uint8_t cr;
        uint8_t multi_acks;
        uint8_t manual_add_contacts;
        float bw;
        uint8_t tx_power_dbm;
        uint8_t telemetry_mode_base;
        uint8_t telemetry_mode_loc;
        uint8_t telemetry_mode_env;
        uint8_t rx_delay_base;
        uint32_t ble_pin;
        uint8_t advert_loc_policy;
        uint8_t buzzer_quiet;
        uint8_t gps_enabled;
      };
      NodePrefs* prefs = (NodePrefs*)_node_prefs;
      prefs->gps_enabled = should_enable ? 1 : 0;
      Serial.printf("[GPS] Updated NodePrefs: gps_enabled=%d\n", prefs->gps_enabled);
    }
    
    return true;
  }
  return false;
}
#else
SensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

#ifndef LORA_CR
  #define LORA_CR      5
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
  // Hardware reset sequence for LoRa module
  // This ensures proper initialization even if module is connected after power-on
  pinMode(P_LORA_RESET, OUTPUT);
  digitalWrite(P_LORA_RESET, LOW);   // Assert reset
  delay(10);                          // Hold for 10ms
  digitalWrite(P_LORA_RESET, HIGH);  // Release reset
  delay(100);                         // Wait for module to boot (SX1262 needs ~50ms)
  
  // Configure RF switch pins only if using external RF switch (DX-LR30)
  // Support both old (P_LORA_*) and new (SX126X_*) naming conventions
  #if defined(SX126X_RXEN) && defined(SX126X_TXEN)
    Serial.println("[LoRa] Configuring external RF switch (DX-LR30)");
    pinMode(RXEN_PIN, OUTPUT);
    pinMode(TXEN_PIN, OUTPUT);
    digitalWrite(RXEN_PIN, LOW);
    digitalWrite(TXEN_PIN, LOW);
    
    // Set RF switch control function
    radio.setRfSwitchPins(RXEN_PIN, TXEN_PIN);
    Serial.printf("[LoRa] RF switch pins: RXEN=%d, TXEN=%d\n", RXEN_PIN, TXEN_PIN);
  #elif defined(P_LORA_RXEN) && defined(P_LORA_TXEN)
    Serial.println("[LoRa] Configuring external RF switch (legacy P_LORA naming)");
    pinMode(RXEN_PIN, OUTPUT);
    pinMode(TXEN_PIN, OUTPUT);
    digitalWrite(RXEN_PIN, LOW);
    digitalWrite(TXEN_PIN, LOW);
    
    // Set RF switch control function
    radio.setRfSwitchPins(RXEN_PIN, TXEN_PIN);
    Serial.printf("[LoRa] RF switch pins: RXEN=%d, TXEN=%d\n", RXEN_PIN, TXEN_PIN);
  #else
    // No external RF switch - DIO2 handles it (Cap LoRa868)
    Serial.println("[LoRa] Using DIO2 for RF switch (Cap LoRa868)");
  #endif
  
  Serial.println("[LoRa] Initializing SX1262...");
  bool init_result = radio.std_init(&spi);
  
  if (init_result) {
    Serial.println("[LoRa] SX1262 initialized successfully");
    // Configure PA for Cap LoRa868 with MAX2659 amplifier
    // PA config: paDutyCycle=0x04, hpMax=0x07, deviceSel=0x00 (SX1262), paLut=0x01
    Serial.println("[LoRa] Configuring PA for Cap LoRa868 (MAX2659)");
    int16_t pa_result = radio.setPaConfig(0x04, 0x07, 0x00, 0x01);
    if (pa_result == 0) {
      Serial.println("[LoRa] PA configured successfully");
    } else {
      Serial.printf("[LoRa] PA config failed: %d\n", pa_result);
    }
  } else {
    Serial.println("[LoRa] ERROR: SX1262 initialization failed!");
  }
  
  return init_result;
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
  Serial.printf("[LoRa] Setting TX power to %d dBm\n", dbm);
  int16_t result = radio.setOutputPower(dbm);
  if (result == 0) {
    Serial.printf("[LoRa] TX power set successfully to %d dBm\n", dbm);
  } else {
    Serial.printf("[LoRa] TX power set failed: %d\n", result);
  }
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
