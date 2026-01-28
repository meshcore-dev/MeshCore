#include "target.h"

#include <Arduino.h>
#include <helpers/ui/UIScreen.h>

BanditBoard board;

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

#ifndef PIN_USER_BTN
#define PIN_USER_BTN (-1)
#endif

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
// MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#if defined(PIN_USER_JOYSTICK)
static AnalogJoystick::JoyADCMapping joystick_mappings[] = {
  { 0, KEY_DOWN },     { 1290, KEY_SELECT }, { 1961, KEY_LEFT },
  { 2668, KEY_RIGHT }, { 3227, KEY_UP },     { 4095, 0 } // IDLE
};

AnalogJoystick analog_joystick(PIN_USER_JOYSTICK, joystick_mappings, 6, KEY_SELECT);
#endif
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

/**
 * Linear interpolation helper for integers
 */
int16_t lerp_int(uint8_t x, uint8_t x0, uint8_t x1, int16_t y0, int16_t y1) {
  if (x1 == x0) return y0;
  return y0 + ((int16_t)(x - x0) * (y1 - y0)) / (x1 - x0);
}

/**
 * Set PA output power with automatic SX1278 and DAC calculation
 *
 * @param target_output_dbm Desired PA output power in dBm (20-30 dBm for 100-1000mW)
 * @param clamp_to_range If true, clamp out-of-range values to min/max instead of failing
 * @return true if successful, false if out of range and clamp_to_range is false
 */
bool setPAOutputPower(uint8_t target_output_dbm, bool clamp_to_range = true) {
  // Validate and clamp range
  if (target_output_dbm < MIN_OUTPUT_DBM) {
    if (clamp_to_range) {
      MESH_DEBUG_PRINT("Warning: Target %u dBm too low, clamping to min %u dBm\n", target_output_dbm,
                       MIN_OUTPUT_DBM);
      target_output_dbm = MIN_OUTPUT_DBM;
    } else {
      MESH_DEBUG_PRINT("Error: Target %u dBm below minimum %u dBm\n", target_output_dbm, MIN_OUTPUT_DBM);
      return false;
    }
  }

  if (target_output_dbm > MAX_OUTPUT_DBM) {
    if (clamp_to_range) {
      MESH_DEBUG_PRINT("Warning: Target %u dBm too high, clamping to max %u dBm\n", target_output_dbm,
                       MAX_OUTPUT_DBM);
      target_output_dbm = MAX_OUTPUT_DBM;
    } else {
      MESH_DEBUG_PRINT("Error: Target %u dBm above maximum %u dBm\n", target_output_dbm, MAX_OUTPUT_DBM);
      return false;
    }
  }

  // Find the calibration points to interpolate between
  int lower_idx = 0;
  int upper_idx = NUM_CAL_POINTS - 1;

  for (int i = 0; i < NUM_CAL_POINTS - 1; i++) {
    if (target_output_dbm >= calibration[i].output_dbm &&
        target_output_dbm <= calibration[i + 1].output_dbm) {
      lower_idx = i;
      upper_idx = i + 1;
      break;
    }
  }

  // Handle exact matches
  if (target_output_dbm == calibration[lower_idx].output_dbm) {
    int8_t sx1278_power = calibration[lower_idx].sx1278_dbm;
    uint8_t dac_value = calibration[lower_idx].dac_value;

    radio.setOutputPower(sx1278_power, true); // RFO output
    dacWrite(DAC_PA_PIN, dac_value);

    MESH_DEBUG_PRINT("Set power: %u dBm (SX1278: %d dBm, DAC: %u)\n", target_output_dbm, sx1278_power,
                     dac_value);
    return true;
  }

  // Linear interpolation between calibration points
  uint8_t lower_out = calibration[lower_idx].output_dbm;
  uint8_t upper_out = calibration[upper_idx].output_dbm;

  // Interpolate SX1278 power (maintaining 18dB gain relationship)
  int16_t sx1278_power = lerp_int(target_output_dbm, lower_out, upper_out, calibration[lower_idx].sx1278_dbm,
                                  calibration[upper_idx].sx1278_dbm);

  // Interpolate DAC value
  int16_t dac_value = lerp_int(target_output_dbm, lower_out, upper_out, calibration[lower_idx].dac_value,
                               calibration[upper_idx].dac_value);

  // Set the calculated values
  radio.setOutputPower((int8_t)sx1278_power, true); // RFO output
  dacWrite(DAC_PA_PIN, (uint8_t)dac_value);

  MESH_DEBUG_PRINT("Set power: %u dBm (SX1278: %d dBm, DAC: %u)\n", target_output_dbm, sx1278_power,
                   dac_value);

  return true;
}

void radio_set_tx_power(uint8_t dbm) {
  setPAOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
