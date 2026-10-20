#include <Arduino.h>
#include "target.h"
#include <helpers/radiolib/ESP32BufferedRadioHal.h>

TDeckBoard board;

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  static ESP32BufferedRadioHal radio_hal(spi);
  RADIO_CLASS radio = new Module(&radio_hal, P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  Wire.begin(18, 8);

#if defined(P_LORA_SCLK)
  const bool ready = radio.std_init(&spi);
#else
  const bool ready = radio.std_init();
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
