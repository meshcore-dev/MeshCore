#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

FeatherRP2040RFMBoard board;

// SX127x has no BUSY line: the Module() args are (cs, irq=DIO0, rst, gpio=DIO1)
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

bool radio_init() {
  rtc_clock.begin(Wire);

  return radio.std_init(&SPI);   // the RFM95 is on the default SPI object (hw SPI1)
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);   // create new random identity
}
