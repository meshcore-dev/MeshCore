#include <helpers/ArduinoHelpers.h>

#include <string.h>

#if defined(ESP32)
  #include <bootloader_random.h>
  #include <esp_random.h>
#elif defined(NRF52_PLATFORM)
  #include <nrf.h>
#endif

namespace {

struct RNGSeedBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint8_t seed[32];
};

static const uint32_t RNG_SEED_MAGIC = 0x474E5253; // "SRNG"
static const uint16_t RNG_SEED_VERSION = 1;

#if defined(STM32_PLATFORM)
static uint32_t stm32HardwareIdMix() {
  uint32_t mix = 0;
  #if defined(HAL_GetUIDw0) && defined(HAL_GetUIDw1) && defined(HAL_GetUIDw2)
    mix = HAL_GetUIDw0() ^ (HAL_GetUIDw1() << 11) ^ (HAL_GetUIDw2() >> 7);
  #elif defined(UID_BASE)
    const volatile uint32_t* uid = (volatile uint32_t*)UID_BASE;
    mix = uid[0] ^ (uid[1] << 11) ^ (uid[2] >> 7);
  #endif
  return mix;
}
#endif

}

namespace mesh {

void initHardwareRNG() {
#if defined(ESP32)
  bootloader_random_enable();
#elif defined(NRF52_PLATFORM)
  NRF_RNG->TASKS_START = 1;
#elif defined(STM32_PLATFORM)
  #if defined(__HAL_RCC_RNG_CLK_ENABLE)
    __HAL_RCC_RNG_CLK_ENABLE();
  #endif
  #if defined(RNG) && defined(RNG_CR_RNGEN)
    SET_BIT(RNG->CR, RNG_CR_RNGEN);
  #endif
#endif
}

void deinitHardwareRNG() {
#if defined(ESP32)
  bootloader_random_disable();
#elif defined(NRF52_PLATFORM)
  NRF_RNG->TASKS_STOP = 1;
#elif defined(STM32_PLATFORM)
  #if defined(RNG) && defined(RNG_CR_RNGEN)
    CLEAR_BIT(RNG->CR, RNG_CR_RNGEN);
  #endif
  #if defined(__HAL_RCC_RNG_CLK_DISABLE)
    __HAL_RCC_RNG_CLK_DISABLE();
  #endif
#endif
}

}

void AsconRNG::gatherEntropy(uint8_t* dest, size_t len) {
  size_t offset = 0;
  while (offset < len) {
    uint32_t r = getHardwareRandom32();
    size_t chunk = len - offset;
    if (chunk > sizeof(r)) chunk = sizeof(r);
    memcpy(dest + offset, &r, chunk);
    offset += chunk;
  }
}

/**
 * This is obviously slow and should not be called directly
 */
uint32_t AsconRNG::getHardwareRandom32() {
  uint32_t r = 0;
#if defined(ESP32)
  r = esp_random();
#elif defined(NRF52_PLATFORM)
  for (int i = 0; i < 4; i++) {
    while (NRF_RNG->EVENTS_VALRDY == 0) {
    }
    NRF_RNG->EVENTS_VALRDY = 0;
    ((uint8_t*)&r)[i] = NRF_RNG->VALUE;
  }
#elif defined(STM32_PLATFORM) && defined(RNG) && defined(RNG_SR_DRDY)
  uint32_t start = micros();
  while ((RNG->SR & RNG_SR_DRDY) == 0) {
    // TODO: Low entropy fallback — only micros()/millis() boot timing.
    // Consider sampling LSBs from a floating ADC pin for
    // additional thermal noise.
    if ((micros() - start) > 2000) {
      uint32_t m = micros();
      uint32_t n = millis();
      uint32_t hardware_id_mix = stm32HardwareIdMix();
      r = (m << 16) ^ (n * 2654435761u) ^ hardware_id_mix;
      break;
    }
  }
  if ((RNG->SR & RNG_SR_DRDY) != 0) {
    r = RNG->DR;
  }
#else
  // TODO: Low entropy fallback: At this point we're desesperate
  uint32_t m = micros();
  uint32_t n = millis();
  r = (m << 16) ^ (n * 2654435761u);
  #if defined(STM32_PLATFORM)
    r ^= stm32HardwareIdMix();
  #endif
#endif
  if (_radio) {
    uint32_t rv = 0;
    uint8_t* rb = (uint8_t*)&rv;
    rb[0] = _radio->randomByte();
    rb[1] = _radio->randomByte();
    rb[2] = _radio->randomByte();
    rb[3] = _radio->randomByte();
    r ^= rv;
  }
  return r;
}

bool AsconRNG::loadSeed(uint8_t* dest, size_t len) {
  if (!_fs || !_seed_path || len < 32) {
    return false;
  }
  if (!_fs->exists(_seed_path)) {
    return false;
  }

  RNGSeedBlob blob;
  memset(&blob, 0, sizeof(blob));

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File file = _fs->open(_seed_path);
#else
  File file = _fs->open(_seed_path, "r");
#endif
  bool valid = false;
  if (file) {
    bool ok = (file.read((uint8_t*)&blob, sizeof(blob)) == (int)sizeof(blob));
    valid = ok && blob.magic == RNG_SEED_MAGIC && blob.version == RNG_SEED_VERSION;
    if (valid) {
      memcpy(dest, blob.seed, 32);
    }
    file.close();
  }
  mesh::Utils::secureClear((uint8_t*)&blob, sizeof(blob));
  return valid;
}

bool AsconRNG::saveSeed() {
  if (!_fs || !_seed_path || !_is_ready) {
    return false;
  }

  RNGSeedBlob blob;
  memset(&blob, 0, sizeof(blob));
  blob.magic = RNG_SEED_MAGIC;
  blob.version = RNG_SEED_VERSION;

  ascon_state_t tmp = _xof;
  ascon_squeeze(&tmp, blob.seed, sizeof(blob.seed));

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(_seed_path);
  File file = _fs->open(_seed_path, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(_seed_path, "w");
#else
  File file = _fs->open(_seed_path, "w", true);
#endif
  if (!file) {
    mesh::Utils::secureClear((uint8_t*)&blob, sizeof(blob));
    return false;
  }

  bool ok = (file.write((const uint8_t*)&blob, sizeof(blob)) == sizeof(blob));
  file.close();
  mesh::Utils::secureClear((uint8_t*)&blob, sizeof(blob));
  return ok;
}

void AsconRNG::initState(const uint8_t* seed, size_t len) {
  ascon_inithash(&_xof);
  ascon_absorb(&_xof, seed, len);
  _is_ready = true;
}

void AsconRNG::begin() {
  if (_is_ready) return;

  uint8_t seed[32];
  bool loaded = loadSeed(seed, sizeof(seed));
  if (loaded) {
    initState(seed, sizeof(seed));
    gatherEntropy(seed, sizeof(seed));
    reseed(seed, sizeof(seed));
  } else {
    gatherEntropy(seed, sizeof(seed));
    initState(seed, sizeof(seed));
    saveSeed();
  }
  mesh::Utils::secureClear(seed, sizeof(seed));
}

void AsconRNG::reseed(const uint8_t* extra, size_t extra_len) {
  if (!_is_ready) {
    begin();
  }

  uint8_t carry[32];
  ascon_squeeze(&_xof, carry, sizeof(carry));

  ascon_inithash(&_xof);
  ascon_absorb(&_xof, (const uint8_t*)"MC-RNG-v1", 9);
  ascon_absorb(&_xof, carry, sizeof(carry));
  if (extra && extra_len > 0) {
    ascon_absorb(&_xof, extra, extra_len);
  }

  saveSeed();
  mesh::Utils::secureClear(carry, sizeof(carry));
}

void AsconRNG::attachPersistence(FILESYSTEM& fs, const char* seed_path) {
  _fs = &fs;
  _seed_path = seed_path ? seed_path : "/seed.rng";
}
