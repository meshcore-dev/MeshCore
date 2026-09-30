#pragma once

#include "target.h"


static SPIClass spi(HSPI); // FSPI = SPI-1 (Display), HSPI = SPI-2 (LoRa)

CustomLR1121 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

CustomLR1121Wrapper radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

GxEPDDisplay display;
// (pin, long_press_mills, reverse, pulldownup, multiclick)
MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true, true);

#if ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors;
#endif

SuperminiBoard board;


bool radio_init() {
	fallback_clock.begin();
	rtc_clock.begin(Wire);

	bool result = radio.std_init(&spi);

	if (result) {
#ifdef RX_BOOSTED_GAIN
		radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif
	}
	return result;
};

mesh::LocalIdentity radio_new_identity() {
	RadioNoiseListener rng(radio);
	return mesh::LocalIdentity(&rng);  // create new random identity
};
