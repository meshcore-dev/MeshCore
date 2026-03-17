#include <Arduino.h>
#include "target.h"

ESP32Board board;
//SenseCapIndicatorBoard board;

static SPIClass spi(FSPI);

// SX1262 pins για SenseCAP Indicator Meshtastic edition
#define LORA_SCLK   5
#define LORA_MISO   4
#define LORA_MOSI   6
#define LORA_NSS    7
#define LORA_DIO1   2
#define LORA_RESET  8
#define LORA_BUSY   3

Module* module = new Module(
    LORA_NSS,
    LORA_DIO1,
    LORA_RESET,
    LORA_BUSY,
    spi
);

SX1262 radio(module);

WRAPPER_CLASS radio_driver(&radio, board);

ESP32RTCClock fallback_clock;
// AutoDiscoverRTCClock rtc_clock(fallback_clock);

EnvironmentSensorManager sensors;

// #ifdef DISPLAY_CLASS
// DISPLAY_CLASS display(&(board.periph_power));
// MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
// #endif


bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  spi.begin(LORA_SCLK, LORA_MISO, LORA_MOSI);

  return radio_driver.std_init(&spi);

  #if defined(P_LORA_SCLK)
    return radio.std_init(&spi);
  #else
    return radio.std_init();
  #endif
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