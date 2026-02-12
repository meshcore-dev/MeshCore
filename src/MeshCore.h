#pragma once

#include <stdint.h>
#include <math.h>

#define MAX_HASH_SIZE        8
#define PUB_KEY_SIZE        32
#define PRV_KEY_SIZE        64
#define SEED_SIZE           32
#define SIGNATURE_SIZE      64
#define MAX_ADVERT_DATA_SIZE  32
#define CIPHER_KEY_SIZE     16
#define CIPHER_BLOCK_SIZE   16

// V1 (AES-ECB + HMAC) - Legacy encryption
#define CIPHER_MAC_SIZE      2
#define PATH_HASH_SIZE       1

// Ascon-128 AEAD encryption with per-packet key derivation
//
// Design goals:
// 1. Minimize airtime (8 bytes overhead: 4-byte counter + 4-byte tag)
// 2. Strong security through per-packet rekeying
// 3. Simple try-decrypt fallback (no capability flags needed)
//
// Per-packet key derivation:
//   packet_key = HMAC-SHA256(shared_secret, counter)[0:16]
//
// This enables a short 4-byte tag because:
// - Each message uses a unique derived key
// - Attacker can't accumulate forgery attempts across messages
// - At LoRa's 500ms/packet, brute forcing 2^32 attempts takes 68 years
//
// Counter: 4 bytes, initialized to random value at boot, increments per packet.
// Random boot offset prevents counter reuse across reboots when RTC is unreliable.
//
// Backwards compatibility:
// - Try Ascon decrypt first, fall back to legacy AES-ECB+HMAC on failure
// - Old clients silently drop Ascon packets (tag check fails)
#define ASCON_KEY_SIZE       16   // Ascon-128 uses 128-bit key
#define ASCON_NONCE_SIZE     16   // Ascon-128 uses 128-bit nonce (internal)
#define ASCON_COUNTER_SIZE    4   // Transmitted counter (random boot offset + sequence)
#define ASCON_TAG_SIZE        4   // 32-bit tag (safe with per-packet rekey)
#define ASCON_OVERHEAD        8   // Total overhead: counter + tag

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