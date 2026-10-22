#include "NRF54Board.h"
#include "NRF54BLEDfu.h"

#include <bluefruit.h>
#include <nrfx_temp.h>

static NRF54BLEDfu bledfu;

void NRF54Board::begin() {
  startup_reason = BD_STARTUP_NORMAL;

  // the nRF54 bootloader leaves its version in TIMER22 CC[0] before starting the app,
  // encoded as (major << 16) | (minor << 8) | patch
  bootloader_version = NRF_TIMER22->CC[0];
  NRF54BLEDfu::bootloader_version = bootloader_version;

  // the bootloader waits for a double reset whenever RESETREAS shows a reset pin press,
  // so clear that bit, otherwise every later reboot takes an extra 500ms
  nrf54ClearResetReason(RESET_RESETREAS_RESETPIN_Msk);

  nrfx_temp_init(NULL, NULL);
}

float NRF54Board::getMCUTemperature() {
  nrfx_temp_measure();
  return nrfx_temp_result_get() / 4.0f;  // result is in 0.25 degC steps
}

bool NRF54Board::getBootloaderVersion(char* version, size_t max_len) {
  // no bootloader, or not ours
  if (bootloader_version == 0 || bootloader_version > 0xFFFFFF) return false;
  snprintf(version, max_len, "%u.%u.%u", (unsigned)(bootloader_version >> 16),
           (unsigned)((bootloader_version >> 8) & 0xFF), (unsigned)(bootloader_version & 0xFF));
  return true;
}

// As on nRF52: advertise the DFU service alone, and keep running until a DFU client asks to start
bool NRF54Board::startOTAUpdate(const char* id, char reply[]) {
  if (bootloader_version < 0x000500 || bootloader_version > 0xFFFFFF) {
    strcpy(reply, "Error: needs nRF54 bootloader 0.5.0 or later");
    return true;
  }
  static bool started = false;
  if (!started) {
    if (!Bluefruit.begin(1, 0)) return false;

    Bluefruit.setTxPower(4);
    Bluefruit.setName(ota_name);

    if (bledfu.begin() != ERROR_NONE) return false;

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);  // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);    // number of seconds in fast mode
    Bluefruit.Advertising.start(0);              // 0 = Don't stop advertising after n seconds
    started = true;
  }

  ble_gap_addr_t addr = Bluefruit.getAddr();
  sprintf(reply, "OK - mac: %02X:%02X:%02X:%02X:%02X:%02X",
          addr.addr[5], addr.addr[4], addr.addr[3], addr.addr[2], addr.addr[1], addr.addr[0]);
  return true;
}

void NRF54Board::loop() {
  NRF54BLEDfu::loop();
}
