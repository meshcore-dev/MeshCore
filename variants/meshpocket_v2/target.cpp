#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

MeshPocketV2 board;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
WRAPPER_CLASS radio_driver(radio, board);
VolatileRTCClock rtc_clock;
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);
DISPLAY_CLASS display(SPI1);
MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);

bool radio_init() {
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
