#include "Utils.h"
#include <AES.h>
#include <SHA256.h>
#include <ChaChaPoly.h>

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

// ========== ChaCha20-Poly1305 Implementation ==========

// Encrypt with ChaCha20-Poly1305
// Returns: total length (nonce + ciphertext + tag)
int Utils::encryptCHACHA(const uint8_t* key, uint8_t* dest, const uint8_t* nonce,
                         const uint8_t* plaintext, int plaintext_len,
                         const uint8_t* aad, int aad_len) {
    ChaChaPoly chacha;

    // Set key and nonce
    chacha.setKey(key, CHACHA_KEY_SIZE);
    chacha.setIV(nonce, CHACHA_NONCE_SIZE);

    // Add AAD if provided
    if (aad && aad_len > 0) {
        chacha.addAuthData(aad, aad_len);
    }

    // Layout: [nonce || ciphertext || tag]
    memcpy(dest, nonce, CHACHA_NONCE_SIZE);

    // Encrypt in-place
    chacha.encrypt(dest + CHACHA_NONCE_SIZE, plaintext, plaintext_len);

    // Generate and append authentication tag
    chacha.computeTag(dest + CHACHA_NONCE_SIZE + plaintext_len, CHACHA_TAG_SIZE);

    return CHACHA_NONCE_SIZE + plaintext_len + CHACHA_TAG_SIZE;
}

// Decrypt with ChaCha20-Poly1305
// Returns: plaintext length on success, 0 on authentication failure
int Utils::decryptCHACHA(const uint8_t* key, uint8_t* dest,
                         const uint8_t* src, int src_len,
                         const uint8_t* aad, int aad_len) {
    // Validate minimum length
    if (src_len < CHACHA_NONCE_SIZE + CHACHA_TAG_SIZE) {
        return 0;
    }

    int ciphertext_len = src_len - CHACHA_NONCE_SIZE - CHACHA_TAG_SIZE;

    // Extract components
    const uint8_t* nonce = src;
    const uint8_t* ciphertext = src + CHACHA_NONCE_SIZE;
    const uint8_t* tag = src + CHACHA_NONCE_SIZE + ciphertext_len;

    ChaChaPoly chacha;

    // Set key and nonce
    chacha.setKey(key, CHACHA_KEY_SIZE);
    chacha.setIV(nonce, CHACHA_NONCE_SIZE);

    // Add AAD if provided
    if (aad && aad_len > 0) {
        chacha.addAuthData(aad, aad_len);
    }

    // Decrypt (must be done before checkTag due to library requirements)
    chacha.decrypt(dest, ciphertext, ciphertext_len);

    // Verify authentication tag (constant-time comparison)
    if (!chacha.checkTag(tag, CHACHA_TAG_SIZE)) {
        // SECURITY: Zero output buffer on auth failure to prevent use of
        // unauthenticated data. Callers must check return value > 0.
        memset(dest, 0, ciphertext_len);
        return 0;
    }

    return ciphertext_len;
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
    #error "No hardware RNG available. ChaCha20-Poly1305 requires secure nonce generation. \
            Supported platforms: ESP32, nRF52, RP2040, STM32 with HAL_RNG."
#endif
}

void Utils::getHighQualityRandom(RNG* rng, uint8_t* dest, size_t size) {
    if (rng) {
        // Use provided RNG (typically RadioNoiseListener for high entropy)
        rng->random(dest, size);
    } else {
        // Fallback to hardware RNG if no RNG provided
        getHardwareRandom(dest, size);
    }
}

// ========== Secure Nonce Generation ==========

// Static state for hybrid nonce generation
static uint32_t s_nonce_boot_id = 0;
static uint32_t s_nonce_counter = 0;

void Utils::generateSecureNonce(uint8_t* nonce) {
    // Hybrid nonce format: [boot_id (4)] [counter (4)] [random (4)]
    // This ensures uniqueness even if:
    // - RNG has bias or periodicity
    // - Many messages sent quickly
    // - Device reboots (new boot_id)

    // Initialize boot_id once per boot with random value
    if (s_nonce_boot_id == 0) {
        getHardwareRandom((uint8_t*)&s_nonce_boot_id, 4);
        // Ensure non-zero (extremely unlikely, but handle it)
        if (s_nonce_boot_id == 0) s_nonce_boot_id = 1;
    }

    // Copy boot_id (bytes 0-3)
    memcpy(nonce, &s_nonce_boot_id, 4);

    // Copy and increment counter (bytes 4-7)
    memcpy(nonce + 4, &s_nonce_counter, 4);
    s_nonce_counter++;

    // Add random salt (bytes 8-11) for additional entropy
    getHardwareRandom(nonce + 8, 4);
}

}
