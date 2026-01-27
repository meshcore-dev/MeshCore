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

// V1 (AES-ECB + HMAC)
#define CIPHER_MAC_SIZE      2
#define PATH_HASH_SIZE       1

// ChaCha20-Poly1305 AEAD encryption
// RFC 8439 specifies 256-bit key, 96-bit nonce, 128-bit tag
// We deviate from RFC in two ways to minimize LoRa airtime:
//
// 1. NONCE COMPRESSION: Transmit 4-byte counter, derive 12-byte nonce via SHA256(key || counter)
//    Security: Equivalent to full nonce - SHA256 output is indistinguishable from random
//
// 2. TAG TRUNCATION: Use 64-bit tag instead of 128-bit (RFC says "MUST NOT" truncate)
//    Security: Forgery probability 1 in 2^64 (vs 2^128). Acceptable for mesh because:
//    - Attacker must be in radio range for each attempt
//    - Each attempt consumes airtime and is detectable
//    - Online brute force at 1000 msg/sec = 292 million years
//    - No known attacks reduce this below brute force
//
// Backwards compatibility: ChaCha packets are sent with PAYLOAD_VER_1 header so old
// repeaters forward them normally. Old clients fail AES decryption and silently drop.
// Updated clients try both crypto methods based on peer capability advertisements.
//
// Trade-off: 8 bytes saved per packet vs. reduced forgery resistance margin
#define CHACHA_KEY_SIZE      32
#define CHACHA_NONCE_SIZE    12   // Internal nonce size (derived from counter + key)
#define CHACHA_COUNTER_SIZE   4   // Transmitted counter size (full nonce derived via SHA256)
#define CHACHA_TAG_SIZE       8   // 64-bit tag (RFC specifies 128-bit, see rationale above)
#define CHACHA_AAD_MARKER  0xCC   // Marker byte used in AAD to identify ChaCha encrypted packets

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