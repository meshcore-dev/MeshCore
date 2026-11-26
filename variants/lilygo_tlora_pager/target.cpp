#include <Arduino.h>
#include "target.h"
#include <helpers/radiolib/ESP32BufferedRadioHal.h>

TLoRaPagerBoard board;

// The LCD and both plug-in radios use the very same SPIClass and bus lock.
static ESP32BufferedRadioHal radio_hal(SPI);
RADIO_CLASS radio = new Module(&radio_hal, P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock rtc_clock;
NmeaGpsDriver gps_driver(Serial1);
MicroNMEALocationProvider gps(gps_driver, &rtc_clock);
EnvironmentSensorManager sensors(gps);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
  UITask ui_task(&board, &display);
#endif

#ifndef LORA_CR
#define LORA_CR 5
#endif

bool radio_init() {
  // The onboard PCF85063A shares an address with the incompatible PCF8563.
  rtc_clock.begin();
  if (!board.peripheralsReady()) return false;
#ifdef USE_SX1262
  const bool ready = radio.std_init();
#else
  static const uint32_t pins[Module::RFSWITCH_MAX_PINS] = {
    RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
  };
  static const Module::RfSwitchMode_t modes[] = {
    {LR11x0::MODE_STBY, {LOW, LOW}}, {LR11x0::MODE_RX, {LOW, HIGH}},
    {LR11x0::MODE_TX, {HIGH, LOW}}, {LR11x0::MODE_TX_HP, {HIGH, LOW}},
    {LR11x0::MODE_TX_HF, {LOW, LOW}}, {LR11x0::MODE_GNSS, {LOW, LOW}},
    {LR11x0::MODE_WIFI, {LOW, LOW}}, END_OF_MODE_TABLE
  };
  const bool ready = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
      RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, 3.0f) == RADIOLIB_ERR_NONE;
  if (ready) {
    radio.setRfSwitchTable(pins, modes);
    radio.explicitHeader();
    radio.setCRC(2);
  }
#endif
#ifdef DISPLAY_CLASS
  if (ready) {
    display.setRenderYieldCallback([]() {
      if (radio_driver.profiles()->enabled()) radio_driver.loop();
    });
  }
#endif
  return ready;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
