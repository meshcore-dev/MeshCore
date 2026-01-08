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
WIOE5SensorManager sensors;

#ifdef DISPLAY_CLASS
  NullDisplayDriver display;
#endif

#ifndef LORA_CR
  #define LORA_CR      5
#endif

bool radio_init() {
  Wire.begin();

  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);

  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, 1.7, 0);

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
  return mesh::LocalIdentity(&rng);  // create new random identity
}

bool WIOE5SensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) { 
  if (!has_bme) return false;

  float temp(NAN), hum(NAN), pres(NAN);

  BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
  BME280::PresUnit presUnit(BME280::PresUnit_hPa);

  bme.read(pres, temp, hum, tempUnit, presUnit);

  telemetry.addTemperature(TELEM_CHANNEL_SELF, temp);
  telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, hum);
  telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, pres);

  return true; 
}

bool WIOE5SensorManager::begin() {
  has_bme = bme.begin();

  return has_bme;
}