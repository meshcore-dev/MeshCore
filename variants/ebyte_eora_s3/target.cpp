#include <Arduino.h>
#include "target.h"

ESP32Board board;

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

#ifndef LORA_CR
  #define LORA_CR      5
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
#ifdef SX126X_DIO3_TCXO_VOLTAGE
  radio.tcxoVoltage = SX126X_DIO3_TCXO_VOLTAGE;
#else
  radio.tcxoVoltage = 1.6f;
#endif

#if defined(P_LORA_SCLK)
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
#endif
  ConfigLoRa_t cfg;
  cfg.frequency = LORA_FREQ;
  cfg.bandwidth = LORA_BW;
  cfg.spreadingFactor = LORA_SF;
  cfg.codingRate = LORA_CR;
  cfg.syncWord = RADIOLIB_LORA_SYNC_WORD_PRIVATE;
  cfg.power = LORA_TX_POWER;
  cfg.preambleLength = 8;
  int status = radio.begin(cfg);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    return false;  // fail
  }
  
  radio.setCRC(1);
  
#if defined(SX126X_RXEN) && defined(SX126X_TXEN)
  radio.setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
#endif

#ifdef SX126X_CURRENT_LIMIT
  radio.setCurrentLimit(SX126X_CURRENT_LIMIT);
#endif
#ifdef SX126X_DIO2_AS_RF_SWITCH
  radio.setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
#endif
#ifdef SX126X_RX_BOOSTED_GAIN
  radio.setRxBoostedGainMode(SX126X_RX_BOOSTED_GAIN);
#endif

  return true;  // success
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
