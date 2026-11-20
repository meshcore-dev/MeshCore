#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

NucleoWL55JCBoard board;

RADIO_CLASS radio = new STM32WLx_Module();

WRAPPER_CLASS radio_driver(radio, board);

// The NUCLEO-WL55JC1 drives an SP3T front-end switch from FE_CTRL1..3 (PC4, PC5, PC3),
// routing the single SMA between RX, RFO_LP and RFO_HP. Pin order and levels are taken
// straight from the core's board definition (variant_NUCLEO_WL55JC1.h) so they stay in
// sync with upstream.
static const uint32_t rfswitch_pins[] = {LORAWAN_RFSWITCH_PINS, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
  {STM32WLx::MODE_IDLE,  {LORAWAN_RFSWITCH_OFF_VALUES}},
  {STM32WLx::MODE_RX,    {LORAWAN_RFSWITCH_RX_VALUES}},
  {STM32WLx::MODE_TX_LP, {LORAWAN_RFSWITCH_RFO_LP_VALUES}},
  {STM32WLx::MODE_TX_HP, {LORAWAN_RFSWITCH_RFO_HP_VALUES}},
  END_OF_MODE_TABLE,
};

VolatileRTCClock rtc_clock;
SensorManager sensors;

#ifndef LORA_CR
  #define LORA_CR      5
#endif

#ifndef STM32WL_TCXO_VOLTAGE
  // The board is fitted with a 32MHz TCXO powered from the radio's DIO3 at 1.7V
  #define STM32WL_TCXO_VOLTAGE 1.7
#endif

#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 22
#endif

bool radio_init() {
  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);

  // Both PA paths are registered above, so RadioLib picks RFO_HP whenever the
  // requested power is above 14dBm, and RFO_LP below that.
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, STM32WL_TCXO_VOLTAGE, 0);

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
