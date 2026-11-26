#pragma once

// Variant headers can advertise more than one optional chip (Generic E22
// defines both USE_SX1262 and USE_SX1268). Validate the concrete transceiver
// selected by the build, not whichever optional USE_* branch appears first.
#define MESH_PROFILE_CHIP_CustomSX1262 1262
#define MESH_PROFILE_CHIP_CustomSTM32WLx 1262
#define MESH_PROFILE_CHIP_CustomSX1268 1268
#define MESH_PROFILE_CHIP_CustomSX1276 1276
#define MESH_PROFILE_CHIP_CustomLLCC68 68
#define MESH_PROFILE_CHIP_CustomLR1110 1110
#define MESH_PROFILE_CHIP_CustomLR1121 1121
#define MESH_PROFILE_CHIP_CustomLR2021 2021
#define MESH_PROFILE_CHIP_(name) MESH_PROFILE_CHIP_##name
#define MESH_PROFILE_CHIP(name) MESH_PROFILE_CHIP_(name)

#if defined(RADIO_CLASS)
  #define MESH_RADIO_PROFILE_CHIP MESH_PROFILE_CHIP(RADIO_CLASS)
#elif defined(USE_SX1276)
  #define MESH_RADIO_PROFILE_CHIP 1276
#elif defined(USE_SX1272)
  #define MESH_RADIO_PROFILE_CHIP 1272
#elif defined(USE_LLCC68)
  #define MESH_RADIO_PROFILE_CHIP 68
#elif defined(USE_LR2021)
  #define MESH_RADIO_PROFILE_CHIP 2021
#elif defined(USE_LR1110)
  #define MESH_RADIO_PROFILE_CHIP 1110
#elif defined(USE_LR1121)
  #define MESH_RADIO_PROFILE_CHIP 1121
#elif defined(USE_SX1268)
  #define MESH_RADIO_PROFILE_CHIP 1268
#else
  #define MESH_RADIO_PROFILE_CHIP 1262
#endif
