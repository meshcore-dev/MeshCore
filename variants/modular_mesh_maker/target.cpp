#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

MMMPromicroBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif

const uint32_t rfswitch_dios[] = {
  RADIOLIB_LR2021_DIO5,
  RADIOLIB_LR2021_DIO6,
  RADIOLIB_LR2021_DIO7,
  RADIOLIB_LR2021_DIO8,
  RADIOLIB_NC
};

static const Module::RfSwitchMode_t rfswitch_table[] = {
  // mode                DIO5  DIO6  DIO7  DIO8
  { LR2021::MODE_STBY,  {LOW,  LOW,  LOW,  LOW }},
  { LR2021::MODE_RX,    {LOW,  LOW,  LOW,  LOW }},   // sub-GHz RX is passive on the F33
  { LR2021::MODE_TX,    {LOW,  HIGH, LOW,  LOW }},   // DIO6 = sub-GHz TXEN (PA key)
  { LR2021::MODE_RX_HF, {HIGH,  LOW,  LOW,  LOW}},   // 2.4G TX (never selected at 915MHz)
  { LR2021::MODE_TX_HF, {LOW,  LOW,  HIGH,  HIGH}},   // 2.4G TX (never selected at 915MHz)
  END_OF_MODE_TABLE,
};

bool radio_init() {
  rtc_clock.begin(Wire);

  int err = radio.std_init(&SPI);
  if (err != 1) return err;

#ifdef RF_SWITCH_TABLE
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
#endif

  return true;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
