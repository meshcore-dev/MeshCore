#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

RAK3401Board board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);

  #if defined(PIN_USER_BTN_ANA)
  MomentaryButton analog_btn(PIN_USER_BTN_ANA, 1000, 20);
  #endif
#endif

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

#ifdef RAK4631_ONBOARD_SX1262_SLEEP
// With a RAK13302 fitted the RAK4631's own SX1262 never gets initialized, so it
// sits in STDBY_RC drawing standby current for the life of the board. Firmware
// cannot cut its supply -- P1.05 is the RF switch rail, not the radio's -- so
// bring it up on its own pins just long enough to put it into cold sleep.
static void sleep_onboard_radio() {
  pinMode(ONBOARD_LORA_ANT_SW, OUTPUT);
  digitalWrite(ONBOARD_LORA_ANT_SW, LOW);   // nothing downstream needs the RF switch

  SPI.setPins(ONBOARD_LORA_MISO, ONBOARD_LORA_SCLK, ONBOARD_LORA_MOSI);
  SPI.begin();

  Module mod(ONBOARD_LORA_NSS, ONBOARD_LORA_DIO_1, ONBOARD_LORA_RESET, ONBOARD_LORA_BUSY, SPI);
  SX1262 onboard(&mod);

  // BUSY is driven low in STDBY_RC and high while asleep, so it doubles as a
  // non-invasive readback of what state we found the chip in and left it in.
  pinMode(ONBOARD_LORA_BUSY, INPUT);
  int busy_before = digitalRead(ONBOARD_LORA_BUSY);

  // begin() resets the chip and leaves it in standby. A TCXO calibration failure
  // still leaves SPI usable, so issue the sleep regardless of what it returns.
  int status = onboard.begin(434.0f, 125.0f, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                             0, 8, ONBOARD_LORA_TCXO_VOLTAGE, false);
  int slept = onboard.sleep(false);         // cold sleep: config discarded, RTC off
  delay(2);
  int busy_after = digitalRead(ONBOARD_LORA_BUSY);
  MESH_DEBUG_PRINTLN("onboard SX1262: begin=%d sleep=%d busy %d->%d",
                     status, slept, busy_before, busy_after);

  SPI.end();   // release SPIM3 so std_init() can repin it onto the RAK13302

  // The SX1262 wakes on an NSS falling edge, so keep NSS driven high from here on.
  digitalWrite(ONBOARD_LORA_NSS, HIGH);
  pinMode(ONBOARD_LORA_NSS, OUTPUT);
}
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
#ifdef RAK4631_ONBOARD_SX1262_SLEEP
  sleep_onboard_radio();
#endif
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

