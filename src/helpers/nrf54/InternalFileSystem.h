#pragma once
// LittleFS-over-RRAM for the nRF54L15 (lolren bare-metal core). Mirrors the STM32
// InternalFileSystem (helpers/stm32) but backs LittleFS with the nRF54L15 RRAM via
// the RRAMC peripheral instead of STM32 flash HAL. Provides the `Adafruit_LittleFS`
// `InternalFS` instance that MeshCore's FILESYSTEM/IdentityStore expect.

#include "Adafruit_LittleFS.h"

#ifndef LFS_RRAM_TOTAL_SIZE
  #define LFS_RRAM_TOTAL_SIZE (16 * 2048)   // 32 KB filesystem (matches STM32 default)
#endif
#define LFS_BLOCK_SIZE (2048)

class InternalFileSystem : public Adafruit_LittleFS {
public:
  InternalFileSystem(void);
  bool begin(void);
};

extern InternalFileSystem InternalFS;

using namespace Adafruit_LittleFS_Namespace;
