#pragma once

#include "Adafruit_LittleFS.h"

// InternalFS region in RRAM, between the end of the app and the core's persistence pages.
// Must match the MEMORY layout in arch/nrf54l/ldscripts/xiao_nrf54lm20a_bootloader.ld.
#ifndef LFS_RRAM_BASE
  #define LFS_RRAM_BASE   0x149000
#endif
#ifndef LFS_RRAM_SIZE
  #define LFS_RRAM_SIZE   0x80000   // 512KB
#endif
#define LFS_BLOCK_SIZE    4096

class InternalFileSystem : public Adafruit_LittleFS {
public:
  InternalFileSystem(void);
  bool begin(void);  // formats and remounts if the mount fails
};

extern InternalFileSystem InternalFS;
