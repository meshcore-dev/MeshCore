#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

WaveshareBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI1);
WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
EnvironmentSensorManager sensors;

bool radio_init() {
  rtc_clock.begin(Wire);

// --- ADD DEBUG PRINTING HERE ---
  Serial.begin(115200);
  // Wait up to 5 seconds for serial console to open so you don't miss the message
  for(int i=0; i<50 && !Serial; i++) delay(100); 

  MESH_DEBUG_PRINTLN("Startup: Board Init");
  MESH_DEBUG_PRINTLN("LORA Pins: NSS=%d, DIO1=%d, RST=%d, BUSY=%d", 
                     P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
  MESH_DEBUG_PRINTLN("SPI Pins: SCK=%d, MOSI=%d, MISO=%d", 
                     P_LORA_SCLK, P_LORA_MOSI, P_LORA_MISO);
  // -------------------------------

  SPI1.setSCK(P_LORA_SCLK);
  SPI1.setTX(P_LORA_MOSI);
  SPI1.setRX(P_LORA_MISO);

  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);

  SPI1.begin(false);


//passing NULL skips init of SPI
  bool success = radio.std_init(NULL);
  
  // --- ADD RESULT PRINTING ---
  if(success) {
      MESH_DEBUG_PRINTLN("Radio Init SUCCESS");
  } else {
      MESH_DEBUG_PRINTLN("Radio Init FAILED (Check Pins!)");
  }
  
  return success;}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
