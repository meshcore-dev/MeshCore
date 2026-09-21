#ifndef WiFiHelper_h
#define WiFiHelper_h

#if defined(ESP32) || defined(RP2040_PLATFORM)
#include <FS.h>
#define FILESYSTEM fs::FS
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <Adafruit_LittleFS.h>
#define FILESYSTEM Adafruit_LittleFS
using namespace Adafruit_LittleFS_Namespace;
#endif

class WiFiHelperClass  {
private:

public:
  WiFiHelperClass() { }

public:
  void setFilesystem(FILESYSTEM *fs);
  void updateSSID(const char *ssid);
  void updatePassphrase(const char *passphrase);
  char *getConfiguredSSID();
  char *getConfiguredPassphrase();
  bool save();
  bool load();
  bool shouldInhibitSleep();
  void forget();
};

extern WiFiHelperClass WiFiHelper;

#endif
