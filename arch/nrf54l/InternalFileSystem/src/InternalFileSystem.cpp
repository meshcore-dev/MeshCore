#include <Arduino.h>
#include <nrf54l15.h>
#include "InternalFileSystem.h"

#define RRAM_WRITEBUF_BYTES  512  // RRAMC write buffer: up to 32 lines of 16 bytes

extern "C" bool nrf54l15_rram_transaction_try_lock(void);
extern "C" void nrf54l15_rram_transaction_unlock(void);

// busy-wait for an RRAMC status bit, give up after 1M polls
static bool rramWait(const volatile uint32_t& reg, uint32_t mask) {
  for (uint32_t n = 1000000; n; n--) {
    if (reg & mask) return true;
  }
  return false;
}

// Write len bytes (multiple of 4) to RRAM at addr, or fill with 0xFF if src is NULL.
// Same RRAMC sequence as the core's bond storage writes: through the write buffer, one
// commit per 512 bytes, and the buffer cleared if anything fails.
static bool rramWrite(uint32_t addr, const uint8_t* src, uint32_t len) {
  if (addr < LFS_RRAM_BASE || addr + len > LFS_RRAM_BASE + LFS_RRAM_SIZE) return false;
  if (!nrf54l15_rram_transaction_try_lock()) return false;

  const uint32_t prev = NRF_RRAMC->CONFIG;
  bool ok = true;
  if ((prev & RRAMC_CONFIG_WRITEBUFSIZE_Msk) &&
      !(NRF_RRAMC->BUFSTATUS.WRITEBUFEMPTY & RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Msk)) {
    // commit anything another writer left in the buffer before resizing it
    NRF_RRAMC->EVENTS_READY = 0;
    NRF_RRAMC->TASKS_COMMITWRITEBUF = 1;
    ok = rramWait(NRF_RRAMC->READY, RRAMC_READY_READY_Msk);
  }
  for (uint32_t done = 0; ok && done < len; ) {
    uint32_t chunk = RRAM_WRITEBUF_BYTES - ((addr + done) % RRAM_WRITEBUF_BYTES);
    if (chunk > len - done) chunk = len - done;

    uint32_t lines = (((addr + done) & 0xF) + chunk + 15) / 16;  // 16-byte lines the chunk touches
    NRF_RRAMC->CONFIG = (prev & ~RRAMC_CONFIG_WRITEBUFSIZE_Msk) | RRAMC_CONFIG_WEN_Msk |
                        (lines << RRAMC_CONFIG_WRITEBUFSIZE_Pos);
    ok = rramWait(NRF_RRAMC->READY, RRAMC_READY_READY_Msk);
    NRF_RRAMC->EVENTS_ACCESSERROR = 0;
    for (uint32_t i = done; ok && i < done + chunk; i += 4) {
      uint32_t w = 0xFFFFFFFF;
      if (src) memcpy(&w, src + i, 4);
      ok = rramWait(NRF_RRAMC->READYNEXT, RRAMC_READYNEXT_READYNEXT_Msk);
      if (ok) *(volatile uint32_t*)(addr + i) = w;
    }
    ok = ok && NRF_RRAMC->EVENTS_ACCESSERROR == 0;
    if (ok) {
      NRF_RRAMC->EVENTS_READY = 0;
      NRF_RRAMC->TASKS_COMMITWRITEBUF = 1;
      ok = rramWait(NRF_RRAMC->READY, RRAMC_READY_READY_Msk);
    }
    done += chunk;
  }
  if (!ok) {
    NRF_RRAMC->EVENTS_READY = 0;
    NRF_RRAMC->TASKS_CLRWRITEBUF = 1;
    rramWait(NRF_RRAMC->READY, RRAMC_READY_READY_Msk);
  }
  NRF_RRAMC->EVENTS_ACCESSERROR = 0;
  NRF_RRAMC->CONFIG = prev & ~RRAMC_CONFIG_WEN_Msk;  // the bootloader can leave writes enabled
  ok = rramWait(NRF_RRAMC->READY, RRAMC_READY_READY_Msk) && ok;
  nrf54l15_rram_transaction_unlock();
  return ok;
}

static int rramRead(const struct lfs_config* c, lfs_block_t block, lfs_off_t off, void* buffer,
                    lfs_size_t size) {
  memcpy(buffer, (const void*)(LFS_RRAM_BASE + block * LFS_BLOCK_SIZE + off), size);
  return LFS_ERR_OK;
}

static int rramProg(const struct lfs_config* c, lfs_block_t block, lfs_off_t off,
                    const void* buffer, lfs_size_t size) {
  uint32_t addr = LFS_RRAM_BASE + block * LFS_BLOCK_SIZE + off;
  return rramWrite(addr, (const uint8_t*)buffer, size) ? LFS_ERR_OK : LFS_ERR_IO;
}

// RRAM has no erase, so fill the block with 0xFF like erased flash
static int rramErase(const struct lfs_config* c, lfs_block_t block) {
  uint32_t addr = LFS_RRAM_BASE + block * LFS_BLOCK_SIZE;
  return rramWrite(addr, NULL, LFS_BLOCK_SIZE) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int rramSync(const struct lfs_config* c) {
  return LFS_ERR_OK;  // every write is committed before it returns
}

static struct lfs_config _InternalFSConfig = {
  .context = NULL,
  .read = rramRead,
  .prog = rramProg,
  .erase = rramErase,
  .sync = rramSync,

  .read_size = 128,
  .prog_size = 128,
  .block_size = LFS_BLOCK_SIZE,
  .block_count = LFS_RRAM_SIZE / LFS_BLOCK_SIZE,
  .lookahead = 128,

  .read_buffer = NULL,
  .prog_buffer = NULL,
  .lookahead_buffer = NULL,
  .file_buffer = NULL
};

InternalFileSystem InternalFS;

InternalFileSystem::InternalFileSystem(void) : Adafruit_LittleFS(&_InternalFSConfig) { }

bool InternalFileSystem::begin(void) {
  if (!Adafruit_LittleFS::begin()) {
    this->format();
    if (!Adafruit_LittleFS::begin()) return false;
  }
  return true;
}
