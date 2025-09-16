#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#ifdef USE_MESHIMI_BOARD
#include "MeshimiBoard.h"
#include "MeshimiSensors.h"
using BOARD_CLASS = MeshimiBoard;
#else
#include <XiaoC6Board.h>
using BOARD_CLASS = XiaoC6Board;
#endif
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>

extern BOARD_CLASS board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
#ifdef USE_MESHIMI_BOARD
extern MeshimiSensorManager sensors;
#else
extern SensorManager sensors;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
