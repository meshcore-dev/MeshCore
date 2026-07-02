/*
 * Zephyr variant glue for the XIAO nRF54L15 + LR2021: the target.h MeshCore's
 * companion expects (board / radio_driver / rtc_clock / sensors + radio_* funcs +
 * LoRa/BLE config). The concrete radio seam (RadioLib LR2021 + ZephyrHal) lives in
 * target.cpp so its headers (with static-member defs) aren't multiply-included.
 */
#pragma once
#include <Arduino.h>
#include <Mesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/RadioLibWrappers.h>   /* RadioLibWrapper API MyMesh uses */

/* 2.4 GHz preset (LR2021 is dual-band)
 * The RadioLib driver auto-switches to the HF path for any frequency in 2400-2500 MHz;
 * target.cpp clamps TX power to the HF PA max of +12 dBm. NOTE: MeshCore's companion caps
 * bandwidth at 500 kHz, so 812/406 (which the chip supports) are rejected by SET_RADIO_PARAMS. */
#define LORA_FREQ_2G4      2450.0f
#define LORA_BW_2G4        500.0f   /* kHz */
#define LORA_SF_2G4        8
#define LORA_CR_2G4        5
#define LORA_TX_POWER_2G4  12       /* dBm — HF PA maximum */

/* LoRa boot defaults: MeshCore EU sub-GHz canon, unless the build defines MC_BAND_2G4 (see
 * CMakeLists), which boots straight onto the 2.4 GHz preset above. 865 MHz stays the normal
 * build; a 2G4 build needs no app interaction (and the choice persists in prefs). The app
 * has no 2.4 GHz preset of its own, presets are app-side and sub-GHz only, so this flag is
 * the practical way to put the node on 2.4 GHz for the RF test. */
#ifdef MC_BAND_2G4
  #define LORA_FREQ      LORA_FREQ_2G4
  #define LORA_BW        LORA_BW_2G4
  #define LORA_SF        LORA_SF_2G4
  #define LORA_CR        LORA_CR_2G4
  #define LORA_TX_POWER  LORA_TX_POWER_2G4
#else
  #ifndef LORA_FREQ
  #define LORA_FREQ      869.618f
  #endif
  #ifndef LORA_BW
  #define LORA_BW        62.5f
  #endif
  #ifndef LORA_SF
  #define LORA_SF        8
  #endif
  #ifndef LORA_CR
  #define LORA_CR        5
  #endif
  #ifndef LORA_TX_POWER
  #define LORA_TX_POWER  22
  #endif
#endif

/* BLE companion */
#ifndef BLE_PIN_CODE
#define BLE_PIN_CODE   123456
#endif
#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "MeshCore-"
#endif

class ZBoard : public mesh::MainBoard {
  public:
	void begin() {}
	uint16_t getBattMilliVolts() override { return 0; }
	const char *getManufacturerName() const override { return "XIAO nRF54L15"; }
	void reboot() override;          /* sys_reboot — defined in target.cpp */
	uint8_t getStartupReason() const override { return _reason; }
	uint8_t _reason = 0;
};

extern ZBoard            board;
extern RadioLibWrapper  &radio_driver;   /* concrete LR2021 wrapper bound in target.cpp */
extern VolatileRTCClock  rtc_clock;
extern SensorManager     sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
