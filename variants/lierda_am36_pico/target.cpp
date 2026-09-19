#include <Arduino.h>
#include "target.h"

#include <helpers/ArduinoHelpers.h>

LierdaAM36Board board;

// LR2021 on dedicated SPI pins (module-internal wiring; see platformio.ini)
static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;   // no-op stub - dev board has no on-board sensors

#ifdef PIN_USER_BTN
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);  // KEY, active low
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  // CustomLR2021::std_init() routes the IRQ to DIO7 (LR2021_IRQ_DIO),
  // brings up SPI on P_LORA_SCLK/MISO/MOSI, and applies LORA_* defaults.
  return radio.std_init(&spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
