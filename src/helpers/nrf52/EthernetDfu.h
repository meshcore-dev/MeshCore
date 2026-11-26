#pragma once

// Reboot into the bootloader's Ethernet firmware update mode.
// Needs a bootloader built with Ethernet DFU (OTAFIX "wiscore_rak4631_ethernet_board");
// other bootloaders ignore the request and simply restart the firmware.
//
// The network config is handed over in no-init RAM, which survives the reset.
// Layout and magic values must match the bootloader's src/eth_dfu.h.
// Host side, once the device has rebooted: see "eth.ota" in docs/cli_commands.md.

#include <Arduino.h>
#include <nrf_soc.h>
#include <nrf_sdm.h>

#ifndef ETHERNET_DFU_PORT
  #define ETHERNET_DFU_PORT 8024
#endif

#define ETH_DFU_GPREGRET_MAGIC  0x45          // 'E'
#define ETH_DFU_PARAMS_ADDR     0x20007FC0UL  // after the BLE DFU bond data at 0x20007F80
#define ETH_DFU_PARAMS_MAGIC    0x44485445UL  // "ETHD"

struct __attribute__((packed)) EthDfuParams {
  uint32_t magic;
  uint8_t  ip[4];
  uint8_t  netmask[4];
  uint8_t  gateway[4];
  uint8_t  mac[6];
  uint16_t port;
  uint32_t check;  // ~(sum of all preceding bytes)
};

static_assert(sizeof(EthDfuParams) == 28, "must match bootloader eth_dfu_params_t");

// The Ethernet DFU bootloader embeds this string (bootloader src/eth_dfu.h ETH_DFU_MARKER)
#define ETH_DFU_MARKER "OTAFIX-ETHDFU-1"

// Bootloader flash region on the nRF52840 (Adafruit bootloader layout)
#define ETH_DFU_BL_START     0xF4000UL  // bootloader start; application and its file system are below
#define ETH_DFU_BL_END       0xFE000UL  // MBR params / bootloader settings follow

// The bootloader places the marker right after its vector table. Only this much of
// the bootloader's start is searched: installing a smaller bootloader over the
// Ethernet one leaves its last pages in flash, and a marker found there is stale.
#define ETH_DFU_MARKER_WINDOW 0x400UL

// Where to start looking for the marker, given UICR NRFFW[0] (bootloader start address).
// An erased or implausible value falls back to the default. The scan must never reach
// below the bootloader: the firmware itself contains the marker string, and so could
// a file in the file system.
static uint32_t ethernet_dfu_scan_start(uint32_t nrffw0) {
  if (nrffw0 < ETH_DFU_BL_START || nrffw0 >= ETH_DFU_BL_END) return ETH_DFU_BL_START;
  return nrffw0;
}

static bool ethernet_dfu_find_marker(const char* p, const char* end) {
  static const char marker[] = ETH_DFU_MARKER;
  const size_t n = sizeof(marker) - 1;
  for (; p + n <= end; p++) {
    if (*p == marker[0] && memcmp(p, marker, n) == 0) return true;
  }
  return false;
}

// True if the installed bootloader supports Ethernet DFU (marker in its first 1 KB).
// The bootloader can't change while the firmware runs, so the check is done once.
static bool ethernet_bootloader_has_dfu() {
  static int8_t cached = -1;
  if (cached < 0) {
    uint32_t start = ethernet_dfu_scan_start(NRF_UICR->NRFFW[0]);
    uint32_t end = start + ETH_DFU_MARKER_WINDOW;
    if (end > ETH_DFU_BL_END) end = ETH_DFU_BL_END;
    cached = ethernet_dfu_find_marker((const char*) start, (const char*) end) ? 1 : 0;
  }
  return cached == 1;
}

// Does not return
static void ethernet_dfu_reboot(const uint8_t ip[4], const uint8_t netmask[4],
                                const uint8_t gateway[4], const uint8_t mac[6]) {
  // GPREGRET first: the SoftDevice call is an SVC, which can't run with IRQs off
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_power_gpregret_clr(0, 0xFF);
    sd_power_gpregret_set(0, ETH_DFU_GPREGRET_MAGIC);
  } else {
    NRF_POWER->GPREGRET = ETH_DFU_GPREGRET_MAGIC;
  }

  EthDfuParams params;
  params.magic = ETH_DFU_PARAMS_MAGIC;
  memcpy(params.ip, ip, 4);
  memcpy(params.netmask, netmask, 4);
  memcpy(params.gateway, gateway, 4);
  memcpy(params.mac, mac, 6);
  params.port = ETHERNET_DFU_PORT;
  uint32_t sum = 0;
  const uint8_t* src = (const uint8_t*) &params;
  for (size_t i = 0; i < offsetof(EthDfuParams, check); i++) sum += src[i];
  params.check = ~sum;

  // The params area is ordinary RAM for the firmware (some static variable lives
  // there): nothing may run after we overwrite it, so block interrupts until the
  // reset. volatile: these stores have no reader in this program.
  __disable_irq();
  volatile uint8_t* dst = (volatile uint8_t*) ETH_DFU_PARAMS_ADDR;
  for (size_t i = 0; i < sizeof(params); i++) dst[i] = src[i];
  NVIC_SystemReset();
}
