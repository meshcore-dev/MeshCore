#pragma once

#include <stdint.h>
#include <stddef.h>
#include <math.h>

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
#define MAX_GROUP_DATA_LENGTH  (MAX_PACKET_PAYLOAD - CIPHER_BLOCK_SIZE - 3)
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

#if BRIDGE_DEBUG && ARDUINO
#define BRIDGE_DEBUG_PRINTLN(F, ...) Serial.printf("%s BRIDGE: " F, getLogDateTime(), ##__VA_ARGS__)
#else
#define BRIDGE_DEBUG_PRINTLN(...) {}
#endif

namespace mesh {

#define  BD_STARTUP_NORMAL     0  // getStartupReason() codes
#define  BD_STARTUP_RX_PACKET  1

// Milliseconds to wait after TX completes before trusting battery ADC readings.
// LoRa TX causes a current spike that sags battery terminal voltage; on LiPo
// cells below ~50% SoC the sag is large enough to cross shutdown/alert thresholds.
#ifndef POST_TX_BATT_SETTLE_MS
#define POST_TX_BATT_SETTLE_MS 250
#endif

class MainBoard {

  bool     _tx_active           = false;
  uint32_t _last_tx_complete_ms = 0;

public:
  // Called by the radio layer — not meant to be overridden.
  void notifyTxStart()                   { _tx_active = true; }
  void notifyTxComplete(uint32_t now_ms) { _tx_active = false; _last_tx_complete_ms = now_ms; }

  // Returns true when it is safe to read the battery ADC (TX not in progress
  // and enough time has elapsed since the last transmission for voltage to recover).
  bool isBattReadSafe(uint32_t now_ms, uint32_t settle_ms = POST_TX_BATT_SETTLE_MS) const {
    return !_tx_active && (now_ms - _last_tx_complete_ms >= settle_ms);
  }

  virtual uint16_t getBattMilliVolts() = 0;
  virtual float getMCUTemperature() { return NAN; }
  virtual bool setAdcMultiplier(float multiplier) { return false; };
  virtual float getAdcMultiplier() const { return 0.0f; }
  virtual const char* getManufacturerName() const = 0;
  virtual void onBeforeTransmit() { }
  virtual void onAfterTransmit() { }
  virtual void reboot() = 0;
  virtual void powerOff() { /* no op */ }
  // Called by example setup() functions to signal that boot is complete.
  // Boards may override to stop a boot-indicator LED sequence or similar.
  // Default no-op: boards that don't care need not implement anything.
  virtual void onBootComplete() { /* no op */ }
  virtual uint32_t getIRQGpio() { return -1; } // not supported. Returns DIO1 (SX1262) and DIO0 (SX127x)
  virtual void sleep(uint32_t secs)  { /* no op */ }
  virtual uint32_t getGpio() { return 0; }
  virtual void setGpio(uint32_t values) {}
  virtual uint8_t getStartupReason() const = 0;
  virtual bool getBootloaderVersion(char* version, size_t max_len) { return false; }
  virtual bool startOTAUpdate(const char* id, char reply[]) { return false; }   // not supported
  virtual bool setLoRaFemLnaEnabled(bool enable) { return false; }
  virtual bool canControlLoRaFemLna() const { return false; }
  virtual bool isLoRaFemLnaEnabled() const { return false; }

  // Power management interface (boards with power management override these)
  virtual bool isExternalPowered() { return false; }
  virtual uint16_t getBootVoltage() { return 0; }
  virtual uint32_t getResetReason() const { return 0; }
  virtual const char* getResetReasonString(uint32_t reason) { return "Not available"; }
  virtual uint8_t getShutdownReason() const { return 0; }
  virtual const char* getShutdownReasonString(uint8_t reason) { return "Not available"; }
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

  /**
   * override in classes that need to periodically update internal state
   */
  virtual void tick() { /* no op */}

  uint32_t getCurrentTimeUnique() {
    uint32_t t = getCurrentTime();
    if (t <= last_unique) {
      return ++last_unique;
    }
    return last_unique = t;
  }
};

}
