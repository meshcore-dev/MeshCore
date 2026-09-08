#include <Arduino.h>
#include "target.h"

WioTrackerL2Board board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
GpsTapStream gps_tap(Serial1);   // GSV sats-in-view / SNR watcher
MicroNMEALocationProvider gps(gps_tap, &rtc_clock);
EnvironmentSensorManager sensors(gps);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif

bool radio_init() {
  MESH_DEBUG_PRINTLN("radio_init: rtc + sx1262 init");
  fallback_clock.begin();
  rtc_clock.begin(Wire);  // Wire already running on 47/48 from board.begin()

  bool ok = radio.std_init(&spi);
  MESH_DEBUG_PRINTLN(ok ? "radio_init: SX1262 OK" : "radio_init: SX1262 FAILED");
  return ok;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
