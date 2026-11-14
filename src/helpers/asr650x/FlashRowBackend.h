#pragma once
#include <Arduino.h>
#include <helpers/asr650x/FlashRecord.h>
#include <string.h>

// Writes straight to the SFLASH user rows through the core's FLASH_update(), one record per 256-byte row.
// (The EEPROM class would rewrite every row of its RAM mirror on each commit, which defeats the A/B scheme.)
extern "C" {
int FLASH_update(uint32_t dst_addr, const void *data, uint32_t size);
int FLASH_read_at(uint32_t address, uint8_t *pData, uint32_t len_bytes);
}

struct FlashRowBackend {
  void read(size_t off, uint8_t *buf, size_t n) { FLASH_read_at(CY_SFLASH_USERBASE + off, buf, n); }

  bool write(size_t off, const uint8_t *buf, size_t n) {
    if (n > 255) return false; // one SFLASH user row
    if (FLASH_update(CY_SFLASH_USERBASE + off, buf, n) != 0) return false;
    uint8_t chk[255];
    FLASH_read_at(CY_SFLASH_USERBASE + off, chk, n);
    return memcmp(chk, buf, n) == 0; // FLASH_update only prints on a failed row program and still returns 0
  }
};
