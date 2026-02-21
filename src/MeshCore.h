#pragma once

#include <stdint.h>
#include <math.h>

#define MAX_HASH_SIZE        8
#define PUB_KEY_SIZE        32
#define PRV_KEY_SIZE        64
#define SEED_SIZE           32
#define SIGNATURE_SIZE      64
#define MAX_ADVERT_DATA_SIZE  32
#define SESSION_KEY_SIZE    32
#define CIPHER_KEY_SIZE     16
#define CIPHER_BLOCK_SIZE   16

// V1
#define CIPHER_MAC_SIZE      2
#define PATH_HASH_SIZE       1

// AEAD-4 (ChaChaPoly) encryption
#define AEAD_TAG_SIZE        4
#define AEAD_NONCE_SIZE      2
#define CONTACT_FLAG_AEAD    0x02   // bit 1 of ContactInfo.flags (bit 0 = favourite)
#define FEAT1_AEAD_SUPPORT   0x0001 // bit 0 of feat1 uint16_t

// AEAD nonce persistence
#define NONCE_PERSIST_INTERVAL  50   // persist every N messages per peer
#define NONCE_BOOT_BUMP         50   // add this on load after dirty boot (must be >= PERSIST_INTERVAL)

// Session key negotiation (Phase 2)
#define REQ_TYPE_SESSION_KEY_INIT   0x08
#define RESP_TYPE_SESSION_KEY_ACCEPT 0x08  // response type byte in PAYLOAD_TYPE_RESPONSE

#define NONCE_REKEY_THRESHOLD       60000  // start renegotiation when nonce exceeds this
#define NONCE_INITIAL_MIN           1000   // min random nonce seed for new contacts
#define NONCE_INITIAL_MAX           50000  // max random nonce seed for new contacts
#define SESSION_KEY_TIMEOUT_MS      180000 // 3 minutes per attempt
#define SESSION_KEY_MAX_RETRIES     3      // attempts per negotiation round
#define MAX_SESSION_KEYS_RAM        8      // max concurrent session key entries in RAM (LRU cache)
#define MAX_SESSION_KEYS_FLASH     48     // max entries in flash file
#define SESSION_KEY_RECORD_SIZE    71     // max bytes per record (with prev_key)
#define SESSION_KEY_RECORD_MIN_SIZE 39   // min bytes per record: [pub_prefix:4][flags:1][nonce:2][key:32]
#define SESSION_FLAG_PREV_VALID   0x01   // prev_session_key is valid for dual-decode
#define SESSION_KEY_STALE_THRESHOLD 50     // sends without recv before fallback to static ECDH
#define SESSION_KEY_ECB_THRESHOLD  100    // sends without recv before fallback to ECB
#define SESSION_KEY_ABANDON_THRESHOLD 255 // sends without recv before clearing AEAD + session key

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

#if BRIDGE_DEBUG && ARDUINO
#define BRIDGE_DEBUG_PRINTLN(F, ...) Serial.printf("%s BRIDGE: " F, getLogDateTime(), ##__VA_ARGS__)
#else
#define BRIDGE_DEBUG_PRINTLN(...) {}
#endif

namespace mesh {

#define  BD_STARTUP_NORMAL     0  // getStartupReason() codes
#define  BD_STARTUP_RX_PACKET  1

class MainBoard {
public:
  virtual uint16_t getBattMilliVolts() = 0;
  virtual float getMCUTemperature() { return NAN; }
  virtual bool setAdcMultiplier(float multiplier) { return false; };
  virtual float getAdcMultiplier() const { return 0.0f; }
  virtual const char* getManufacturerName() const = 0;
  virtual void onBeforeTransmit() { }
  virtual void onAfterTransmit() { }
  virtual void reboot() = 0;
  virtual void powerOff() { /* no op */ }
  virtual void sleep(uint32_t secs)  { /* no op */ }
  virtual uint32_t getGpio() { return 0; }
  virtual void setGpio(uint32_t values) {}
  virtual uint8_t getStartupReason() const = 0;
  virtual bool getBootloaderVersion(char* version, size_t max_len) { return false; }
  virtual bool startOTAUpdate(const char* id, char reply[]) { return false; }   // not supported

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