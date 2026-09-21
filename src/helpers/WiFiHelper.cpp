#include "WiFiHelper.h"
#include <WiFi.h>

static FILESYSTEM *_fs = nullptr;
void WiFiHelperClass::setFilesystem(FILESYSTEM *fs) {
  _fs = fs;
}

const char *_filename = "/wifi.config";
static char _ssid[64] = "";
static char _passphrase[64] = "";

void WiFiHelperClass::updateSSID(const char *ssid) {
  strncpy(_ssid, ssid, sizeof(_ssid) - 1);
  _ssid[sizeof(_ssid) - 1] = '\0';
  WiFi.disconnect();
  WiFi.begin(_ssid, _passphrase);
}

void WiFiHelperClass::updatePassphrase(const char *passphrase) {
  strncpy(_passphrase, passphrase, sizeof(_passphrase) - 1);
  _passphrase[sizeof(_passphrase) - 1] = '\0';
  WiFi.disconnect();
  WiFi.begin(_ssid, _passphrase);
}

char *WiFiHelperClass::getConfiguredSSID() {
  if (_ssid[0] == '\0') {
    return "<not set>";
  }
  return _ssid;
}

char *WiFiHelperClass::getConfiguredPassphrase() {
  if (_passphrase[0] == '\0') {
    return "<not set>";
  }
  return _passphrase;
}

bool WiFiHelperClass::save() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(_filename);
  File file = _fs->open(_filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(_filename, "w");
#else
  File file = _fs->open(_filename, "w", true);
#endif
  if (file) {
    bool success = (file.write((const uint8_t *)_ssid, sizeof(_ssid)) == sizeof(_ssid));
    success =
        success && (file.write((const uint8_t *)_passphrase, sizeof(_passphrase)) == sizeof(_passphrase));
    file.close();
    return success;
  }
  return false;
}

bool WiFiHelperClass::load() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File file = _fs->open(_filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(_filename, "r");
#else
  File file = _fs->open(_filename, "r", true);
#endif
  if (file) {
    size_t ssid_len = file.read((uint8_t *)_ssid, sizeof(_ssid));
    size_t passphrase_len = file.read((uint8_t *)_passphrase, sizeof(_passphrase));
    file.close();
    WiFi.begin(_ssid, _passphrase);
    return true;
  }
  return false;
}

void WiFiHelperClass::forget() {
  WiFi.disconnect();
  _ssid[0] = '\0';
  _passphrase[0] = '\0';
  if (_fs) {
    _fs->remove(_filename);
  }
}

WiFiClass WiFi;
