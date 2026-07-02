#include <Arduino.h>
#include "InternalFileSystem.h"
#include <nrf54l15.h>
#include <string.h>

// Filesystem region, resident in RRAM
// `const` forces it into .rodata (RRAM/flash) at a stable, linker-allocated
// address (NOT .data/.bss in RAM); so it survives reboot. We write to it at
// runtime via the RRAMC peripheral (RRAM is uniformly writable regardless of the
// .rodata label, the same mechanism the core's EEPROM library uses).
// Force into a .rodata subsection so the linker places it in FLASH/RRAM (the
// linker routes *(.rodata*) -> FLASH). Without an explicit section the `volatile`
// initialised array lands in .data (RAM) and would NOT persist.
__attribute__((section(".rodata.lfs_region"), aligned(LFS_BLOCK_SIZE)))
static const volatile uint8_t g_lfsRegion[LFS_RRAM_TOTAL_SIZE] = { 0 };
#define LFS_BASE  ((uint32_t)(uintptr_t)g_lfsRegion)

// RRAMC write primitive (adapted from the core's EEPROM library)
static constexpr uint32_t kRramcBase = 0x5004B000UL;
static constexpr uint32_t kRramcSpin = 600000UL;

static inline NRF_RRAMC_Type* rramc() { return reinterpret_cast<NRF_RRAMC_Type*>(kRramcBase); }

static bool waitReady(NRF_RRAMC_Type* r, uint32_t spin) {
  while (spin-- > 0U) {
    if (((r->READY & RRAMC_READY_READY_Msk) >> RRAMC_READY_READY_Pos) == RRAMC_READY_READY_Ready) return true;
  }
  return false;
}
static bool waitReadyNext(NRF_RRAMC_Type* r, uint32_t spin) {
  while (spin-- > 0U) {
    if (((r->READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) >> RRAMC_READYNEXT_READYNEXT_Pos) == RRAMC_READYNEXT_READYNEXT_Ready) return true;
  }
  return false;
}

static bool rramWrite(uint32_t addr, const uint8_t* src, size_t len) {
  NRF_RRAMC_Type* const r = rramc();
  const uint32_t prev = r->CONFIG;
  r->CONFIG = prev | RRAMC_CONFIG_WEN_Msk;
  bool ok = waitReady(r, kRramcSpin);
  if (ok) {
    r->EVENTS_ACCESSERROR = 0U;
    for (size_t i = 0; i < len; ++i) {
      if (!waitReadyNext(r, kRramcSpin)) { ok = false; break; }
      *reinterpret_cast<volatile uint8_t*>(addr + static_cast<uint32_t>(i)) = src[i];
    }
    if (r->EVENTS_ACCESSERROR != 0U) ok = false;
  }
  if (ok) {
    r->EVENTS_READY = 0U;
    r->TASKS_COMMITWRITEBUF = 1U;
    ok = waitReady(r, kRramcSpin);
  }
  r->CONFIG = prev;
  return ok;
}

// LittleFS block device over RRAM
static int _rram_read(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer, lfs_size_t size) {
  (void)c;
  if (!buffer || !size) return LFS_ERR_INVAL;
  memcpy(buffer, (const void*)(LFS_BASE + block * LFS_BLOCK_SIZE + off), size);  // RRAM is memory-mapped
  return LFS_ERR_OK;
}
static int _rram_prog(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, const void* buffer, lfs_size_t size) {
  (void)c;
  return rramWrite(LFS_BASE + block * LFS_BLOCK_SIZE + off, (const uint8_t*)buffer, size) ? LFS_ERR_OK : LFS_ERR_IO;
}
static int _rram_erase(const struct lfs_config* c, lfs_block_t block) {
  (void)c; (void)block;
  return LFS_ERR_OK;   // RRAM needs no erase-before-write (unlike NOR flash)
}
static int _rram_sync(const struct lfs_config* c) { (void)c; return LFS_ERR_OK; }

struct lfs_config _InternalFSConfig = {
  .context = NULL,
  .read  = _rram_read,
  .prog  = _rram_prog,
  .erase = _rram_erase,
  .sync  = _rram_sync,

  .read_size   = LFS_BLOCK_SIZE,
  .prog_size   = LFS_BLOCK_SIZE,
  .block_size  = LFS_BLOCK_SIZE,
  .block_count = LFS_RRAM_TOTAL_SIZE / LFS_BLOCK_SIZE,
  .lookahead   = 128,

  .read_buffer      = NULL,
  .prog_buffer      = NULL,
  .lookahead_buffer = NULL,
  .file_buffer      = NULL
};

InternalFileSystem InternalFS;

InternalFileSystem::InternalFileSystem(void) : Adafruit_LittleFS(&_InternalFSConfig) { }

bool InternalFileSystem::begin(void) {
  // mount; on failure format then mount again
  if (!Adafruit_LittleFS::begin()) {
    this->format();
    if (!Adafruit_LittleFS::begin()) return false;
  }
  return true;
}
