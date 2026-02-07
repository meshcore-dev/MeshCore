#include <Arduino.h>
#include <esp_system.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

#ifndef WAVESHARE_ALLOW_NO_RADIO
  #define WAVESHARE_ALLOW_NO_RADIO 0
#endif

#ifndef WAVESHARE_RADIO_LR11X0
  #define WAVESHARE_RADIO_LR11X0 0
#endif

#ifndef LORA_CR
  #define LORA_CR 5
#endif

WaveshareESP32C6Board board;

#if defined(P_LORA_SCLK)
  static SPIClass spi(0);
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
#endif
static bool radio_available = false;

#ifdef DISPLAY_CLASS
static void showRadioInitError(int status) {
  if (!display.isOn()) return;
  char line2[32];
  snprintf(line2, sizeof(line2), "code: %d", status);
  display.startFrame();
  display.setCursor(0, 0);
  display.setColor(DisplayDriver::LIGHT);
  display.print("Radio init failed");
  display.setCursor(0, 10);
  display.print(line2);
  display.endFrame();
}
#endif

#if WAVESHARE_RADIO_LR11X0 && defined(RF_SWITCH_TABLE)
static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
  RADIOLIB_LR11X0_DIO5,
  RADIOLIB_LR11X0_DIO6,
  RADIOLIB_NC,
  RADIOLIB_NC,
  RADIOLIB_NC
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                 DIO5  DIO6
  { LR11x0::MODE_STBY,   {LOW,  LOW }},
  { LR11x0::MODE_RX,     {HIGH, LOW }},
  { LR11x0::MODE_TX,     {HIGH, HIGH }},
  { LR11x0::MODE_TX_HP,  {LOW,  HIGH }},
  { LR11x0::MODE_TX_HF,  {LOW,  LOW }},
  { LR11x0::MODE_GNSS,   {LOW,  LOW }},
  { LR11x0::MODE_WIFI,   {LOW,  LOW }},
  END_OF_MODE_TABLE,
};
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

#if WAVESHARE_RADIO_LR11X0
  #ifdef LR11X0_DIO3_TCXO_VOLTAGE
    float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
  #else
    float tcxo = 1.8f;
  #endif
  int status = RADIOLIB_ERR_NONE;

#if defined(P_LORA_SCLK)
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
#endif
  // Some LR1121 modules require DIO3 TCXO drive; others use an always-on XTAL/TCXO.
  // If tcxo is configured as 0.0, probe common TCXO voltages before giving up.
  float tcxo_candidates[3] = { tcxo, 1.8f, 3.3f };
  int tcxo_tries = (fabsf(tcxo) <= 0.001f) ? 3 : 1;
  float used_tcxo = tcxo;
  for (int i = 0; i < tcxo_tries; i++) {
    used_tcxo = tcxo_candidates[i];
    status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                         RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, used_tcxo);
    if (status == RADIOLIB_ERR_NONE) break;
  }

  if (status == RADIOLIB_ERR_NONE) {
    radio.setCRC(2);
    radio.explicitHeader();
  #if defined(RF_SWITCH_TABLE)
    radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
  #endif
  #ifdef RX_BOOSTED_GAIN
    radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
  #endif
    radio_available = true;
  } else {
#ifdef DISPLAY_CLASS
    showRadioInitError(status);
#endif
    radio_available = false;
  }
#else
  #if defined(P_LORA_SCLK)
    spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    radio_available = radio.std_init(&spi);
  #else
    radio_available = radio.std_init();
  #endif
#endif

#if WAVESHARE_ALLOW_NO_RADIO
  return true;
#else
  return radio_available;
#endif
}

uint32_t radio_get_rng_seed() {
#if WAVESHARE_ALLOW_NO_RADIO
  if (!radio_available) {
    return (uint32_t)esp_random();
  }
#endif
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
#if WAVESHARE_ALLOW_NO_RADIO
  if (!radio_available) return;
#endif
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
#if WAVESHARE_ALLOW_NO_RADIO
  if (!radio_available) return;
#endif
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
#if WAVESHARE_ALLOW_NO_RADIO
  if (!radio_available) {
    StdRNG rng;
    rng.begin((long)esp_random());
    return mesh::LocalIdentity(&rng);
  }
#endif
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
