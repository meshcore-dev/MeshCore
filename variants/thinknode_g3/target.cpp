#include <Arduino.h>
#include "target.h"

ThinkNodeG3Board board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

bool radio_init() {
  // Stock firmware drives GPIO 45 high once at init (antenna switch / PE_EN).
  // ESP32Board does not know about P_LORA_EN, so do it here before radio begin.
#ifdef P_LORA_EN
  pinMode(P_LORA_EN, OUTPUT);
  digitalWrite(P_LORA_EN, HIGH);
#endif

  fallback_clock.begin();
  rtc_clock.begin(Wire);
  return radio.std_init(&spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
