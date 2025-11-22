#include <Arduino.h>
#include "target.h"
#include "pins_arduino.h"

ArduinoNessoN1Board board;

#if defined(P_LORA_SCLK)
  static SPIClass spi(0);
  // replace P_LORA_RESET with -1 to indicate RESET is handled outside
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, -1, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, -1, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

bool radio_init() {
  MESH_DEBUG_PRINTLN("radio_init()");
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  MESH_DEBUG_PRINTLN("set Nesso N1 pin modes and default states...");
  pinMode(LORA_ENABLE, OUTPUT); // RESET
  pinMode(LORA_ANTENNA_SWITCH, OUTPUT); // ANTENNA_SWITCH
  pinMode(LORA_LNA_ENABLE, OUTPUT); // LNA_ENABLE
  pinMode(LCD_BACKLIGHT, OUTPUT);
  pinMode(BEEP_PIN, OUTPUT);

  // Toggle LoRa reset via expander
  MESH_DEBUG_PRINTLN("Enable LoRa...");
  digitalWrite(LORA_ENABLE, LOW);
  delay(10);
  digitalWrite(LORA_ENABLE, HIGH);

  // Configure antenna switch and LNA
  digitalWrite(LORA_ANTENNA_SWITCH, HIGH); // enable antenna switch
  digitalWrite(LORA_LNA_ENABLE, HIGH); // enable LNA

  // Configure initial state of further devices on expander
  digitalWrite(LCD_BACKLIGHT, LOW);
  digitalWrite(BEEP_PIN, LOW);

  // Toggle LCD backlight to show the device has powered on until we get the screen working
  digitalWrite(LCD_BACKLIGHT, HIGH);
  // digitalWrite(BEEP_PIN, HIGH);
  delay(2000);
  digitalWrite(LCD_BACKLIGHT, LOW);
  // digitalWrite(BEEP_PIN, LOW);

  MESH_DEBUG_PRINTLN("radio.std_init() and return...");
#if defined(P_LORA_SCLK)
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
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

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}


