#pragma once

#include <stdint.h>

#define MAX_HASH_SIZE        8
#define PUB_KEY_SIZE        32
#define PRV_KEY_SIZE        64
#define SEED_SIZE           32
#define SIGNATURE_SIZE      64
#define MAX_ADVERT_DATA_SIZE  32
#define CIPHER_KEY_SIZE     16
#define CIPHER_BLOCK_SIZE   16

// V1
#define CIPHER_MAC_SIZE      2
#define PATH_HASH_SIZE       1

#define MAX_PACKET_PAYLOAD  184
#define MAX_PATH_SIZE        64
#define MAX_TRANS_UNIT      255

#if MESH_DEBUG && ARDUINO
  #include <Arduino.h>
  #define MESH_DEBUG_PRINT(F, ...) Serial.printf("DEBUG: " F, ##__VA_ARGS__)
  #define MESH_DEBUG_PRINTLN(F, ...) Serial.printf("DEBUG: " F "\n", ##__VA_ARGS__)
#else
  #define MESH_DEBUG_PRINT(...) {}
  #define MESH_DEBUG_PRINTLN(...) {}
#endif

namespace mesh {

#define  BD_STARTUP_NORMAL     0  // getStartupReason() codes
#define  BD_STARTUP_RX_PACKET  1

class MainBoard {
public:
  virtual uint16_t getBattMilliVolts() = 0;
  virtual const char* getManufacturerName() const = 0;
  virtual void onBeforeTransmit() { }
  virtual void onAfterTransmit() { }
  virtual void reboot() = 0;
  virtual void powerOff() { /* no op */ }
  virtual uint32_t getGpio() { return 0; }
  virtual void setGpio(uint32_t values) {}
  virtual uint8_t getStartupReason() const = 0;
  virtual bool startOTAUpdate(const char* id, char reply[]) { return false; }   // not supported
};

/**
 * An abstraction of the device's Realtime Clock.
*/
class RTCClock {
  uint32_t last_unique;
protected:
  RTCClock() { last_unique = 0; }

public:
  /**
   * \returns  the current time. in UNIX epoch seconds.
  */
  virtual uint32_t getCurrentTime() = 0;

  /**
   * \param time  current time in UNIX epoch seconds.
  */
  virtual void setCurrentTime(uint32_t time) = 0;

  uint32_t getCurrentTimeUnique() {
    uint32_t t = getCurrentTime();
    if (t <= last_unique) {
      return ++last_unique;
    }
    return last_unique = t;
  }
};

}