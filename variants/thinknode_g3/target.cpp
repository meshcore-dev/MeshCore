#include <Arduino.h>
#include "target.h"

ThinknodeG3Board board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

bool radio_init() {
  // Ensure antenna switch (GPIO45) is driven HIGH before SX1262 init
  pinMode(45, OUTPUT);
  digitalWrite(45, HIGH);

  fallback_clock.begin();
  rtc_clock.begin(Wire);
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  return radio.std_init(&spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
