#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

#if defined(UI_HAS_ROTARY_INPUT)
#include "HeltecRC52RotaryInput.h"
#endif

#ifdef ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
#endif

HeltecRC52Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock, PIN_GPS_RESET, PIN_GPS_EN);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
#ifdef HELTEC_RC52_WITH_DISPLAY
DISPLAY_CLASS display(&SPI1);
#else
DISPLAY_CLASS display;
#endif
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#if defined(UI_HAS_ROTARY_INPUT)
static HeltecRC52RotaryInput rotaryInputImpl(&board.periph_power);
RotaryInput& rotary_input = rotaryInputImpl;
#endif
#endif

bool radio_init()
{
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity()
{
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
