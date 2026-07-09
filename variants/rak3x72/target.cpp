#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

RAK3x72Board board;

RADIO_CLASS radio = new STM32WLx_Module();

WRAPPER_CLASS radio_driver(radio, board);

static const uint32_t rfswitch_pins[] = {LORAWAN_RFSWITCH_PINS,  RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
  {STM32WLx::MODE_IDLE,  {LOW,  LOW}},
  {STM32WLx::MODE_RX,    {HIGH, LOW}},
  {STM32WLx::MODE_TX_LP, {LOW, HIGH}},
  {STM32WLx::MODE_TX_HP, {LOW, HIGH}},
  END_OF_MODE_TABLE,
};

VolatileRTCClock rtc_clock;
SensorManager sensors;

#ifndef LORA_CR
  #define LORA_CR      5
#endif

#ifndef STM32WL_TCXO_VOLTAGE
  // TCXO set to 0 for RAK3172
  #define STM32WL_TCXO_VOLTAGE 0
#endif

#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 22
#endif

bool radio_init() {
//  rtc_clock.begin(Wire);

  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);

  radio.tcxoVoltage = STM32WL_TCXO_VOLTAGE;
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
