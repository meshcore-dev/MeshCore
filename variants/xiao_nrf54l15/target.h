#pragma once
// Variant glue for the XIAO nRF54L15 + LR2021 (declarations).
// Mirrors MeshCore's target.h convention: extern the board/radio globals and
// declare the radio_* entry points; target.cpp defines them.

#include <Arduino.h>
#include <Mesh.h>           // top-level MeshCore header: makes arduino-cli pull in the MeshCore library
#include <RadioLib.h>
#include <helpers/ArduinoHelpers.h>

// --- Board/radio config. In a full PlatformIO variant these are build flags; ---
// --- defined here (guarded) so the .cpp compile standalone under arduino-cli. ---
// --- MUST precede the CustomLR2021 include: its std_init() expands LORA_*.    ---
// MeshCore canonical EU defaults (match platformio.ini arduino_base) so this node
// interoperates with any stock MeshCore EU device, not just the 2nd Wio-LR2021.
// freq/bw/sf/cr MUST match between any two nodes that need to hear each other.
#ifndef LORA_FREQ
  #define LORA_FREQ      869.618
#endif
#ifndef LORA_BW
  #define LORA_BW        62.5
#endif
#ifndef LORA_SF
  #define LORA_SF        8
#endif
#ifndef LORA_CR
  #define LORA_CR        5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  22   // LR2021 LF PA capable; reduce for EU ERP limits / duty cycle as needed
#endif

// LR2021 wiring on the Wio-LR2021 (verified on hardware)
#ifndef P_LORA_NSS
  #define P_LORA_NSS     PIN_D3
#endif
#ifndef P_LORA_DIO_1
  #define P_LORA_DIO_1   PIN_D0
#endif
#ifndef P_LORA_RESET
  #define P_LORA_RESET   PIN_D2
#endif
#ifndef P_LORA_BUSY
  #define P_LORA_BUSY    PIN_D1
#endif

#include <helpers/radiolib/CustomLR2021Wrapper.h>
#include "XiaoNrf54l15Board.h"
#include <helpers/SensorManager.h>

// Globals defined in target.cpp
extern XiaoNrf54l15Board   board;
extern CustomLR2021Wrapper radio_driver;
extern VolatileRTCClock    rtc_clock;
extern SensorManager       sensors;     // no-op stub (full repeater expects a `sensors` global)

// Radio entry points (the contract the firmware / mesh engine call)
bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
