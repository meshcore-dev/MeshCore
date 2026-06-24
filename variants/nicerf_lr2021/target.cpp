#include <Arduino.h>
#include "target.h"

NiceRFLR2021Board board;

// On ESP32-C3, FSPI (SPI peripheral 0) is the only general-purpose SPI bus.
// Using SPIClass(0) per maintainer suggestion. If this returns all-zeros on
// your board, define USE_ESPIDF_HAL in platformio.ini to use the ESP-IDF SPI
// HAL workaround (see EspIdfHal.h for details).
#ifdef USE_ESPIDF_HAL
#include "EspIdfHal.h"
static EspIdfHal hal(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
RADIO_CLASS radio(new Module(&hal, P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET,
                             P_LORA_BUSY));
#else
static SPIClass spi(0);
RADIO_CLASS radio(new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET,
                             P_LORA_BUSY, spi));
#endif
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors;
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

#ifndef USE_ESPIDF_HAL
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
#else
  hal.init();
#endif
  return radio.std_init(NULL);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
