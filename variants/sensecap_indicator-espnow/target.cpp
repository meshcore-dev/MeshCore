#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

ESP32Board board;

ESPNOWRadio radio_driver;

ESP32RTCClock rtc_clock;
#if defined(ENV_INCLUDE_GPS)
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, (mesh::RTCClock*)&rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors = EnvironmentSensorManager();
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  #ifdef PIN_USER_BTN
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
  #endif
#endif

bool radio_init() {
  rtc_clock.begin();

  radio_driver.init();

  return true;  // success
}

// NOTE: as we are using the WiFi radio, the ESP_IDF will have enabled hardware RNG:
//    https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/random.html
class ESP_RNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override {
    esp_fill_random(dest, sz);
  }
};

mesh::LocalIdentity radio_new_identity() {
  ESP_RNG rng;
  return mesh::LocalIdentity(&rng);  // create new random identity
}
