#include <Arduino.h>
#include "target.h"

// Variant glue for the XIAO nRF54L15 + LR2021 (definitions).

XiaoNrf54l15Board board;

CustomLR2021 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
CustomLR2021Wrapper radio_driver(radio, board);

VolatileRTCClock rtc_clock;
SensorManager sensors;    // no-op stub (Phase 5 full repeater expects a `sensors` global)

bool radio_init() {
  return radio.std_init(&SPI);   // sets irqDioNum=8 + begin(tcxoVoltage=0); see CustomLR2021.h
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

void radio_set_tx_power(int8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // new random identity from radio RSSI noise
}
