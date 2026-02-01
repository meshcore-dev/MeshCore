#include "Utils.h"
#include <AES.h>
#include <SHA256.h>

// LibAscon - lightweight AEAD cipher (CC0 license)
// https://github.com/TheMatjaz/LibAscon
extern "C" {
#include <ascon.h>
}

#ifdef ARDUINO
  #include <Arduino.h>
#endif

#ifdef ESP32
  #include <esp_random.h>
#endif

#ifdef NRF52_PLATFORM
  #include <nrf.h>
  #include <nrf_sdm.h>
  #include <nrf_soc.h>
#endif

namespace mesh {

uint32_t RNG::nextInt(uint32_t _min, uint32_t _max) {
  uint32_t num;
  random((uint8_t *) &num, sizeof(num));
  return (num % (_max - _min)) + _min;
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t* msg, int msg_len) {
  SHA256 sha;
  sha.update(msg, msg_len);
  sha.finalize(hash, hash_len);
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t* frag1, int frag1_len, const uint8_t* frag2, int frag2_len) {
  SHA256 sha;
  sha.update(frag1, frag1_len);
  sha.update(frag2, frag2_len);
  sha.finalize(hash, hash_len);
}

int Utils::decrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  AES128 aes;
  uint8_t* dp = dest;
  const uint8_t* sp = src;

  aes.setKey(shared_secret, CIPHER_KEY_SIZE);
  while (sp - src < src_len) {
    aes.decryptBlock(dp, sp);
    dp += 16; sp += 16;
  }

  return sp - src;  // will always be multiple of 16
}

int Utils::encrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  AES128 aes;
  uint8_t* dp = dest;

  aes.setKey(shared_secret, CIPHER_KEY_SIZE);
  while (src_len >= 16) {
    aes.encryptBlock(dp, src);
    dp += 16; src += 16; src_len -= 16;
  }
  if (src_len > 0) {  // remaining partial block
    uint8_t tmp[16];
    memset(tmp, 0, 16);
    memcpy(tmp, src, src_len);
    aes.encryptBlock(dp, tmp);
    dp += 16;
  }
  return dp - dest;  // will always be multiple of 16
}

int Utils::encryptThenMAC(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  int enc_len = encrypt(shared_secret, dest + CIPHER_MAC_SIZE, src, src_len);

  SHA256 sha;
  sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
  sha.update(dest + CIPHER_MAC_SIZE, enc_len);
  sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, dest, CIPHER_MAC_SIZE);

  return CIPHER_MAC_SIZE + enc_len;
}

int Utils::MACThenDecrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  if (src_len <= CIPHER_MAC_SIZE) return 0;  // invalid src bytes

  uint8_t hmac[CIPHER_MAC_SIZE];
  {
    SHA256 sha;
    sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
    sha.update(src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
    sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, hmac, CIPHER_MAC_SIZE);
  }
  if (memcmp(hmac, src, CIPHER_MAC_SIZE) == 0) {
    return decrypt(shared_secret, dest, src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
  }
  return 0; // invalid HMAC
}

static const char hex_chars[] = "0123456789ABCDEF";

void Utils::toHex(char* dest, const uint8_t* src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    *dest++ = hex_chars[b >> 4];
    *dest++ = hex_chars[b & 0x0F];
    len--;
  }
  *dest = 0;
}

void Utils::printHex(Stream& s, const uint8_t* src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    s.print(hex_chars[b >> 4]);
    s.print(hex_chars[b & 0x0F]);
    len--;
  }
}

static uint8_t hexVal(char c) {
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= '0' && c <= '9') return c - '0';
  return 0;
}

bool Utils::isHexChar(char c) {
  return c == '0' || hexVal(c) > 0;
}

bool Utils::fromHex(uint8_t* dest, int dest_size, const char *src_hex) {
  int len = strlen(src_hex);
  if (len != dest_size*2) return false;  // incorrect length

  uint8_t* dp = dest;
  while (dp - dest < dest_size) {
    char ch = *src_hex++;
    char cl = *src_hex++;
    *dp++ = (hexVal(ch) << 4) | hexVal(cl);
  }
  return true;
}

int Utils::parseTextParts(char* text, const char* parts[], int max_num, char separator) {
  int num = 0;
  char* sp = text;
  while (*sp && num < max_num) {
    parts[num++] = sp;
    while (*sp && *sp != separator) sp++;
    if (*sp) {
       *sp++ = 0;  // replace the seperator with a null, and skip past it
    }
  }
  // if we hit the maximum parts, make sure LAST entry does NOT have separator
  while (*sp && *sp != separator) sp++;
  if (*sp) {
    *sp = 0;  // replace the separator with null
  }
  return num;
}

// ========== Ascon Encryption: Ascon-128 with Per-Packet Key Derivation ==========
//
// Security design:
// 1. Per-packet key derivation: key = HMAC-SHA256(shared_secret, counter)[0:16]
//    This enables short 4-byte tags (safe because key changes every packet)
// 2. Nonce: counter zero-padded to 16 bytes
// 3. Tag: 4 bytes (2^32 forgery attempts, but key changes before exhaustion)
//
// Packet format: [counter:4][ciphertext:N][tag:4] = 8 bytes overhead

// Derive per-packet key from shared secret and counter
// key = HMAC-SHA256(shared_secret, counter)[0:16]
static void derivePacketKey(uint8_t* packet_key, const uint8_t* shared_secret, const uint8_t* counter) {
    SHA256 sha;
    uint8_t hmac_out[32];
    sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
    sha.update(counter, ASCON_COUNTER_SIZE);
    sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, hmac_out, 32);
    memcpy(packet_key, hmac_out, ASCON_KEY_SIZE);
}

// Expand 4-byte counter to 16-byte nonce (zero-padded)
static void expandNonce(uint8_t* nonce, const uint8_t* counter) {
    memset(nonce, 0, ASCON_NONCE_SIZE);
    memcpy(nonce + ASCON_NONCE_SIZE - ASCON_COUNTER_SIZE, counter, ASCON_COUNTER_SIZE);
}

// Ascon Encrypt with Ascon-128 and per-packet key derivation
// Returns: total length (counter + ciphertext + tag)
int Utils::encryptAscon(const uint8_t* shared_secret, uint8_t* dest,
                        const uint8_t* plaintext, int plaintext_len) {
    // Generate unique counter
    uint8_t counter[ASCON_COUNTER_SIZE];
    generateCounter(counter);

    // Derive per-packet key
    uint8_t packet_key[ASCON_KEY_SIZE];
    derivePacketKey(packet_key, shared_secret, counter);

    // Expand counter to full nonce
    uint8_t nonce[ASCON_NONCE_SIZE];
    expandNonce(nonce, counter);

    // Write counter to output
    memcpy(dest, counter, ASCON_COUNTER_SIZE);

    // Encrypt with Ascon-128 using LibAscon offline API
    // Output: ciphertext directly after counter, tag after ciphertext
    uint8_t* ciphertext_out = dest + ASCON_COUNTER_SIZE;
    uint8_t* tag_out = dest + ASCON_COUNTER_SIZE + plaintext_len;

    ascon_aead128_encrypt(
        ciphertext_out,            // ciphertext output
        tag_out,                   // tag output (truncated)
        packet_key,                // 16-byte key
        nonce,                     // 16-byte nonce
        NULL,                      // no associated data
        plaintext,                 // plaintext input
        0,                         // assoc_data_len
        (size_t)plaintext_len,     // plaintext_len
        ASCON_TAG_SIZE             // tag_len (4 bytes - built-in truncation!)
    );

    // Clear sensitive material
    memset(packet_key, 0, ASCON_KEY_SIZE);

    return ASCON_COUNTER_SIZE + plaintext_len + ASCON_TAG_SIZE;
}

// Ascon Decrypt with Ascon-128 and per-packet key derivation
// Returns: plaintext length on success, 0 on authentication failure
int Utils::decryptAscon(const uint8_t* shared_secret, uint8_t* dest,
                        const uint8_t* src, int src_len) {
    // Validate minimum length
    if (src_len < ASCON_COUNTER_SIZE + ASCON_TAG_SIZE) {
        return 0;
    }

    int ciphertext_len = src_len - ASCON_COUNTER_SIZE - ASCON_TAG_SIZE;

    // Extract components
    const uint8_t* counter = src;
    const uint8_t* ciphertext = src + ASCON_COUNTER_SIZE;
    const uint8_t* tag = src + ASCON_COUNTER_SIZE + ciphertext_len;

    // Derive per-packet key
    uint8_t packet_key[ASCON_KEY_SIZE];
    derivePacketKey(packet_key, shared_secret, counter);

    // Expand counter to full nonce
    uint8_t nonce[ASCON_NONCE_SIZE];
    expandNonce(nonce, counter);

    // Decrypt with Ascon-128 using LibAscon offline API
    // LibAscon supports truncated tags directly via expected_tag_len!
    bool valid = ascon_aead128_decrypt(
        dest,                      // plaintext output
        packet_key,                // 16-byte key
        nonce,                     // 16-byte nonce
        NULL,                      // no associated data
        ciphertext,                // ciphertext input
        tag,                       // expected tag (truncated)
        0,                         // assoc_data_len
        (size_t)ciphertext_len,    // ciphertext_len
        ASCON_TAG_SIZE             // expected_tag_len (4 bytes - built-in truncation!)
    );

    // Clear sensitive material
    memset(packet_key, 0, ASCON_KEY_SIZE);

    if (!valid) {
        // Authentication failed - zero output
        memset(dest, 0, ciphertext_len);
        return 0;
    }

    return ciphertext_len;
}

// Unified decryption: try Ascon first, fall back to legacy
int Utils::decryptAuto(const uint8_t* shared_secret, uint8_t* dest,
                       const uint8_t* src, int src_len, bool* was_ascon) {
    // Try Ascon first - this is the happy path for updated clients
    int len = decryptAscon(shared_secret, dest, src, src_len);
    if (len > 0) {
        if (was_ascon) *was_ascon = true;
        return len;
    }

    // Fall back to legacy (AES-ECB + HMAC) for old clients
    len = MACThenDecrypt(shared_secret, dest, src, src_len);
    if (len > 0) {
        if (was_ascon) *was_ascon = false;
    }
    return len;
}

// ========== Hardware RNG Implementation ==========

void Utils::getHardwareRandom(uint8_t* dest, size_t size) {
#ifdef ESP32
    // ESP32: Use hardware TRNG (very fast, ~1μs)
    for (size_t i = 0; i < size; i += 4) {
        uint32_t rand_val = esp_random();
        size_t bytes_to_copy = (size - i) < 4 ? (size - i) : 4;
        memcpy(&dest[i], &rand_val, bytes_to_copy);
    }

#elif defined(NRF52_PLATFORM)
    // nRF52: Check if SoftDevice is enabled (BLE active)
    uint8_t sd_enabled = 0;
    sd_softdevice_is_enabled(&sd_enabled);

    if (sd_enabled) {
        // SoftDevice is active - must use its API to avoid hardfault
        size_t remaining = size;
        size_t offset = 0;
        while (remaining > 0) {
            uint8_t available = 0;
            // Check how many random bytes are available
            sd_rand_application_bytes_available_get(&available);
            if (available > 0) {
                uint8_t to_get = (remaining < available) ? remaining : available;
                sd_rand_application_vector_get(dest + offset, to_get);
                offset += to_get;
                remaining -= to_get;
            }
            // If not enough bytes available, wait briefly for RNG to generate more
            if (remaining > 0 && available == 0) {
                delayMicroseconds(10);
            }
        }
    } else {
        // SoftDevice not active - safe to use RNG peripheral directly
        NRF_RNG->TASKS_START = 1;
        for (size_t i = 0; i < size; i++) {
            while (!NRF_RNG->EVENTS_VALRDY);  // Wait for random byte
            dest[i] = NRF_RNG->VALUE;
            NRF_RNG->EVENTS_VALRDY = 0;
        }
        NRF_RNG->TASKS_STOP = 1;  // Stop RNG to save power
    }

#elif defined(RP2040_PLATFORM)
    // RP2040: Use ROSC-based hardware random (fast, ~1μs)
    for (size_t i = 0; i < size; i += 4) {
        uint32_t rand_val = rp2040.hwrand32();
        size_t bytes_to_copy = (size - i) < 4 ? (size - i) : 4;
        memcpy(&dest[i], &rand_val, bytes_to_copy);
    }

#elif defined(STM32_PLATFORM)
    // STM32WL: Use hardware RNG peripheral
    // Most STM32 variants with LoRa (STM32WL) have hardware RNG
    #if defined(HAL_RNG_MODULE_ENABLED)
        extern RNG_HandleTypeDef hrng;
        for (size_t i = 0; i < size; i += 4) {
            uint32_t rand_val;
            HAL_RNG_GenerateRandomNumber(&hrng, &rand_val);
            size_t bytes_to_copy = (size - i) < 4 ? (size - i) : 4;
            memcpy(&dest[i], &rand_val, bytes_to_copy);
        }
    #else
        // No hardware RNG - fall back but mix with more entropy sources
        // WARNING: This path should be avoided for v2 encryption
        MESH_DEBUG_PRINTLN("WARNING: STM32 without HAL_RNG - using weak entropy!");
        static uint32_t stm32_entropy_pool = 0xDEADBEEF;
        for (size_t i = 0; i < size; i += 4) {
            // Mix multiple sources for better entropy
            stm32_entropy_pool ^= micros();
            stm32_entropy_pool ^= (millis() << 16);
            stm32_entropy_pool = (stm32_entropy_pool * 1103515245) + 12345;  // LCG step
            uint32_t rand_val = stm32_entropy_pool ^ (i * 0x5DEECE66D);
            size_t bytes_to_copy = (size - i) < 4 ? (size - i) : 4;
            memcpy(&dest[i], &rand_val, bytes_to_copy);
        }
    #endif

#else
    // No secure RNG available - FAIL COMPILATION for v2 encryption safety
    #error "No hardware RNG available. Ascon-128 AEAD requires secure nonce generation. \
            Supported platforms: ESP32, nRF52, RP2040, STM32 with HAL_RNG."
#endif
}

// ========== Counter Generation for Ascon ==========

// Counter with random boot offset for Ascon nonce.
// Since we derive a fresh key per packet (key = HMAC-SHA256(shared_secret, counter)),
// the counter only needs to be unique, not unpredictable. However, if timestamp
// replay protection can't be trusted (nodes rebooting to hardcoded dates), we need
// the counter itself to be unlikely to repeat across reboots.
//
// Using random boot offset: even with weak RNG, the probability of collision is
// low enough (1 in 2^32 with perfect RNG, worse with bad RNG but still helpful).
static uint32_t s_ascon_counter = 0;
static bool s_counter_initialized = false;

void Utils::initAsconCounter(RNG& rng) {
    if (!s_counter_initialized) {
        // Random starting point to avoid counter reuse across reboots
        rng.random((uint8_t*)&s_ascon_counter, sizeof(s_ascon_counter));
        s_counter_initialized = true;
    }
}

void Utils::generateCounter(uint8_t* counter) {
    memcpy(counter, &s_ascon_counter, ASCON_COUNTER_SIZE);
    s_ascon_counter++;
}

}
