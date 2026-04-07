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

}

namespace mesh {

void initHardwareRNG() {
#if defined(ESP32)
  bootloader_random_enable();
#elif defined(NRF52_PLATFORM)
  NRF_RNG->TASKS_START = 1;
#endif
}

void deinitHardwareRNG() {
#if defined(ESP32)
  bootloader_random_disable();
#elif defined(NRF52_PLATFORM)
  NRF_RNG->TASKS_STOP = 1;
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
#else
  uint32_t m = micros();
  uint32_t n = millis();
  r = (m << 16) ^ (n * 2654435761u);
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
  if (!file) {
    return false;
  }

  bool ok = (file.read((uint8_t*)&blob, sizeof(blob)) == (int)sizeof(blob));
  file.close();

  if (!ok || blob.magic != RNG_SEED_MAGIC || blob.version != RNG_SEED_VERSION) {
    return false;
  }

  memcpy(dest, blob.seed, 32);
  return true;
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
    return false;
  }

  bool ok = (file.write((const uint8_t*)&blob, sizeof(blob)) == sizeof(blob));
  file.close();
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
  if (loadSeed(seed, sizeof(seed))) {
    initState(seed, sizeof(seed));
    reseed(NULL, 0);
    return;
  }

  gatherEntropy(seed, sizeof(seed));
  initState(seed, sizeof(seed));
  saveSeed();
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
}

void AsconRNG::attachPersistence(FILESYSTEM& fs, const char* seed_path) {
  _fs = &fs;
  _seed_path = seed_path ? seed_path : "/seed.rng";
}
