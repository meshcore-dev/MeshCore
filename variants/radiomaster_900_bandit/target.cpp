#include "target.h"

#include <Arduino.h>

ESP32Board board;

#if defined(P_LORA_SCLK)
static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, spi);
#else
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {

#ifdef PA_FAN_EN
  pinMode(PA_FAN_EN, OUTPUT);
  digitalWrite(PA_FAN_EN, 1);
#endif

  fallback_clock.begin();
  rtc_clock.begin(Wire);

#if defined(P_LORA_SCLK)
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

#if defined(RADIOMASTER_900_BANDIT_NANO) || defined(RADIOMASTER_900_BANDIT)
// Structure to hold DAC and DB values
typedef struct {
  uint8_t dac;
  uint8_t db;
} DACDB;

// Interpolation function
DACDB interpolate(uint8_t dbm, uint8_t dbm1, uint8_t dbm2, DACDB val1, DACDB val2) {
  DACDB result;
  double fraction = (double)(dbm - dbm1) / (dbm2 - dbm1);
  result.dac = (uint8_t)(val1.dac + fraction * (val2.dac - val1.dac));
  result.db = (uint8_t)(val1.db + fraction * (val2.db - val1.db));
  return result;
}

// Function to find the correct DAC and DB values based on dBm using interpolation
DACDB getDACandDB(uint8_t dbm) {
  // Predefined values
  static const struct {
    uint8_t dbm;
    DACDB values;
  }
#ifdef RADIOMASTER_900_BANDIT_NANO
  dbmToDACDB[] = {
    { 20, { 168, 2 } }, // 100mW
    { 24, { 148, 6 } }, // 250mW
    { 27, { 128, 9 } }, // 500mW
    { 30, { 90, 12 } }  // 1000mW
  };
#endif
#ifdef RADIOMASTER_900_BANDIT
  dbmToDACDB[] = {
    { 20, { 165, 2 } }, // 100mW
    { 24, { 155, 6 } }, // 250mW
    { 27, { 142, 9 } }, // 500mW
    { 30, { 110, 10 } } // 1000mW
  };
#endif
  const int numValues = sizeof(dbmToDACDB) / sizeof(dbmToDACDB[0]);

  // Find the interval dbm falls within and interpolate
  for (int i = 0; i < numValues - 1; i++) {
    if (dbm >= dbmToDACDB[i].dbm && dbm <= dbmToDACDB[i + 1].dbm) {
      return interpolate(dbm, dbmToDACDB[i].dbm, dbmToDACDB[i + 1].dbm, dbmToDACDB[i].values,
                         dbmToDACDB[i + 1].values);
    }
  }

  // Return a default value if no match is found and default to 100mW
#ifdef RADIOMASTER_900_BANDIT_NANO
  DACDB defaultValue = { 168, 2 };
#endif
#ifdef RADIOMASTER_900_BANDIT
  DACDB defaultValue = { 165, 2 };
#endif
  return defaultValue;
}
#endif

void radio_set_tx_power(uint8_t dbm) {
#if defined(RADIOMASTER_900_BANDIT_NANO) || defined(RADIOMASTER_900_BANDIT)
  // DAC and DB values based on dBm using interpolation
  DACDB dacDbValues = getDACandDB(dbm);
  int8_t powerDAC = dacDbValues.dac;
  dbm = dacDbValues.db;
#endif

  // enable PA
#ifdef PIN_PA_EN
#ifdef PA_DAC_EN
#if defined(RADIOMASTER_900_BANDIT_NANO) || defined(RADIOMASTER_900_BANDIT)
  // Use calculated DAC value
  dacWrite(PIN_PA_EN, powerDAC);
#else
  // Use Value set in /*/variant.h
  dacWrite(PIN_PA_EN, PA_LEVEL);
#endif
#endif
#endif

  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
