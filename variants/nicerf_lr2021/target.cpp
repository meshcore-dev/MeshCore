#include <Arduino.h>
#include "target.h"
#include "EspIdfHal.h"

NiceRFLR2021Board board;

static EspIdfHal hal(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
RADIO_CLASS radio(new Module(&hal, P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET,
                             P_LORA_BUSY));
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors;
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  hal.init();
  return radio.std_init(NULL);
}

uint32_t radio_get_rng_seed() { return radio.random(0x7FFFFFFF); }

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) { radio.setOutputPower(dbm); }

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
