#include <Arduino.h>
#include <SPI.h>
#include "target.h"

DFRobotLoRaBoard board;

// FSPI is SPI2. The ST7735 driver takes HSPI (SPI3); the default SPIClass
// constructor on ESP32-S3 is HSPI and would collide with the display.
static SPIClass spi(FSPI);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display(&board.periph_power);
  // Active-low button, internal pull-up.
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  return radio.std_init(&spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
