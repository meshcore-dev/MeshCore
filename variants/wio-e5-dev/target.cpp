#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

WIOE5Board board;

RADIO_CLASS radio = new STM32WLx_Module();

WRAPPER_CLASS radio_driver(radio, board);

static const uint32_t rfswitch_pins[] = {PA4,  PA5,  RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
  {STM32WLx::MODE_IDLE,  {LOW,  LOW}},
  {STM32WLx::MODE_RX,    {HIGH, LOW}},
  {STM32WLx::MODE_TX_HP, {LOW, HIGH}},  // for LoRa-E5 mini
//  {STM32WLx::MODE_TX_LP, {HIGH, HIGH}},   // for LoRa-E5-LE mini
  END_OF_MODE_TABLE,
};

VolatileRTCClock rtc_clock;
SensorManager sensors;

#ifndef LORA_CR
  #define LORA_CR      5
#endif

bool radio_init() {
//  rtc_clock.begin(Wire);
  
// #ifdef SX126X_DIO3_TCXO_VOLTAGE
//   float tcxo = SX126X_DIO3_TCXO_VOLTAGE;
// #else
//   float tcxo = 1.6f;
// #endif

  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);

  radio.tcxoVoltage = 1.7f;
  ConfigLoRa_t cfg;
  cfg.frequency = LORA_FREQ;
  cfg.bandwidth = LORA_BW;
  cfg.spreadingFactor = LORA_SF;
  cfg.codingRate = LORA_CR;
  cfg.syncWord = RADIOLIB_LORA_SYNC_WORD_PRIVATE;
  cfg.power = LORA_TX_POWER;
  cfg.preambleLength = 16;
  int status = radio.begin(cfg);

  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    return false;  // fail
  }
    
  #ifdef RX_BOOSTED_GAIN
    radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
  #endif
 
  radio.setCRC(1);
  
  return true;  // success
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
