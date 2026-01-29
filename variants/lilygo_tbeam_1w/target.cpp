#include <Arduino.h>
#include "target.h"

TBeam1WBoard board;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

static SPIClass spi;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  // Initialize SPI for radio
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);

  // GPS serial initialized by EnvironmentSensorManager::begin()

  bool success = radio.std_init(&spi);
  if (success) {
    // T-Beam 1W has external PA requiring longer ramp time (>800us recommended)
    // RADIOLIB_SX126X_PA_RAMP_800U = 0x05
    radio.setTxParams(LORA_TX_POWER, RADIOLIB_SX126X_PA_RAMP_800U);
  }
  return success;
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
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
