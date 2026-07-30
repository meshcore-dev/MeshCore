#include "target.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

WaveshareBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RST, P_LORA_DIO_1, SPI);
WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

MicroNMEALocationProvider nmea(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(nmea);

bool radio_init() {
  Serial.begin(115200);
  delay(2000);
  
  Serial1.setRX(PIN_GPS_RX);
  Serial1.setTX(PIN_GPS_TX);
  Serial1.begin(9600);
  rtc_clock.begin(Wire);
  SPI.begin();
  bool result = radio.std_init(NULL);
  
  if (LittleFS.begin()) {
    Serial.println("LittleFS mounted OK");
  } else {
  Serial.println("LittleFS mount failed, formatting...");
  LittleFS.format();
  if (LittleFS.begin()) {
    Serial.println("LittleFS formatted and mounted OK");
  } else {
    Serial.println("LittleFS still failed!");
  }
  }
  
  return result;
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
  return mesh::LocalIdentity(&rng);
}