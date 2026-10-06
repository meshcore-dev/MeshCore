#pragma once

// Heltec CubeCell GPS-6502 (HTCC-AB02S): legacy ASR6502 board (ARM Cortex-M0+, 128 KB flash, 16 KB RAM, fixed 2 KB
// stack), SX1262, Air530Z GPS, 0.96" SSD1306 OLED. Companion over USB/UART only (no BLE, no WiFi).

#include <Arduino.h>
#include <Mesh.h>
#include <helpers/asr650x/CubeCellRadio.h>
#include <helpers/asr650x/CubeCellRTC.h>
#include <helpers/asr650x/TxGate.h>
#include "CubeCellBoard.h"
#include "CubeCellSensors.h"

#ifndef min
  #define min(a, b) ((a) < (b) ? (a) : (b))   // the CubeCell core does not provide min()
#endif

extern CubeCellBoard board;
extern CubeCellRadio radio_driver;
extern CubeCellSensors sensors;
extern CubeCellRTC rtc_clock;

bool radio_init();
mesh::LocalIdentity radio_new_identity();   // placeholder derived from the chip ID, NOT random (see target.cpp)
bool asr650x_identity_is_placeholder(const mesh::Identity& id);
extern uint8_t asr650x_boot_pool[4];        // fold of the boot-time Radio.Random() outputs (CubeCellRadio.cpp)
