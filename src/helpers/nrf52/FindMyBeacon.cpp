#include "FindMyBeacon.h"

// Gated on the opt-in feature flag so the unit stays inert even if a variant's build_src_filter
// happens to glob helpers/nrf52/*.cpp.
#ifdef WITH_FINDMY_BEACON

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <bluefruit.h>
#include <InternalFileSystem.h>
#include <base64.hpp>
#include <Mesh.h>
#include "ble_gap.h"
#include "../NRF52Board.h"

using namespace Adafruit_LittleFS_Namespace;

// Advertising interval in units of 0.625 ms. ~2s by default to keep idle current low;
// override per-build with -D FINDMY_ADV_INTERVAL=<units>.
#ifndef FINDMY_ADV_INTERVAL
#define FINDMY_ADV_INTERVAL 3200
#endif

// How often loop() consults the clock for a day rollover. Daily rotation needs no finer
// precision (the rollover does not have to be exactly at midnight), so check hourly to keep RTC
// reads (I2C) rare. Override with -D FINDMY_CHECK_INTERVAL_MS.
#ifndef FINDMY_CHECK_INTERVAL_MS
#define FINDMY_CHECK_INTERVAL_MS 3600000UL  // 1 hour
#endif

// Compile-time Unix epoch of this build, parsed from __DATE__/__TIME__ (build-machine local
// time; day precision is all that matters here). Used as the "is the clock set?" threshold: a
// node with no battery-backed RTC falls back to VolatileRTCClock, which defaults to 15 May 2024
// - necessarily before this build - so an unset clock reads below the threshold and the beacon
// stays on slot 0 until a real time is set. A 1-day margin absorbs build/set/timezone skew.
// Self-updating across releases, so it never goes stale. (Macros, not constexpr, for C++11.)
//   __DATE__ = "Mmm dd yyyy"   __TIME__ = "hh:mm:ss"
#define FM_MON ( (__DATE__[0]=='J'&&__DATE__[1]=='a') ? 1 \
               : (__DATE__[0]=='F') ? 2 \
               : (__DATE__[0]=='M'&&__DATE__[2]=='r') ? 3 \
               : (__DATE__[0]=='A'&&__DATE__[1]=='p') ? 4 \
               : (__DATE__[0]=='M'&&__DATE__[2]=='y') ? 5 \
               : (__DATE__[0]=='J'&&__DATE__[2]=='n') ? 6 \
               : (__DATE__[0]=='J'&&__DATE__[2]=='l') ? 7 \
               : (__DATE__[0]=='A'&&__DATE__[1]=='u') ? 8 \
               : (__DATE__[0]=='S') ? 9 \
               : (__DATE__[0]=='O') ? 10 \
               : (__DATE__[0]=='N') ? 11 : 12 )
#define FM_DAY  ( (__DATE__[4]==' ' ? 0 : __DATE__[4]-'0')*10 + (__DATE__[5]-'0') )
#define FM_YEAR ( (__DATE__[7]-'0')*1000 + (__DATE__[8]-'0')*100 + (__DATE__[9]-'0')*10 + (__DATE__[10]-'0') )
#define FM_HMS  ( ((__TIME__[0]-'0')*10+(__TIME__[1]-'0'))*3600L + ((__TIME__[3]-'0')*10+(__TIME__[4]-'0'))*60 + ((__TIME__[6]-'0')*10+(__TIME__[7]-'0')) )
// days since 1970-01-01 (Howard Hinnant's days_from_civil), with y' = year - (month<=2)
#define FM_YP   (FM_YEAR - (FM_MON <= 2 ? 1 : 0))
#define FM_ERA  ((FM_YP >= 0 ? FM_YP : FM_YP-399) / 400)
#define FM_YOE  (FM_YP - FM_ERA*400)
#define FM_DOY  ((153*(FM_MON + (FM_MON>2 ? -3 : 9)) + 2)/5 + FM_DAY - 1)
#define FM_DOE  (FM_YOE*365 + FM_YOE/4 - FM_YOE/100 + FM_DOY)
#define FM_DAYS ((long)FM_ERA*146097 + FM_DOE - 719468)

static const uint32_t FINDMY_MIN_VALID_TIME = (uint32_t)(FM_DAYS*86400L + FM_HMS) - 86400UL;

#define FINDMY_FILE    "/findmy"
#define FINDMY_VERSION 1

FindMyBeacon findmy_beacon;

bool FindMyBeacon::load() {
  _enabled = 0;
  _count = 0;
  if (!InternalFS.exists(FINDMY_FILE)) return false;
  File f = InternalFS.open(FINDMY_FILE);
  if (!f) return false;

  uint8_t version = 0;
  uint16_t count = 0;
  f.read((uint8_t *)&version, sizeof(version));
  f.read((uint8_t *)&_enabled, sizeof(_enabled));
  f.read((uint8_t *)&count, sizeof(count));
  if (version != FINDMY_VERSION) { f.close(); return false; }
  if (count > FINDMY_MAX_KEYS) count = FINDMY_MAX_KEYS;
  for (uint16_t i = 0; i < count; i++) {
    f.read(_keys[i], 28);
  }
  _count = count;
  f.close();
  return true;
}

void FindMyBeacon::save() {
  InternalFS.remove(FINDMY_FILE);
  File f = InternalFS.open(FINDMY_FILE, FILE_O_WRITE);
  if (!f) return;
  uint8_t version = FINDMY_VERSION;
  f.write((uint8_t *)&version, sizeof(version));
  f.write((uint8_t *)&_enabled, sizeof(_enabled));
  f.write((uint8_t *)&_count, sizeof(_count));
  for (uint16_t i = 0; i < _count; i++) {
    f.write(_keys[i], 28);
  }
  f.close();
}

void FindMyBeacon::startAdvertising(const uint8_t key[28]) {
  // Static-random BLE address derived from the first 6 key bytes. ble_gap_addr_t.addr is
  // little-endian (addr[0] = LSB); the MSB's top two bits mark a static random address.
  ble_gap_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
  addr.addr[5] = key[0] | 0xC0;
  addr.addr[4] = key[1];
  addr.addr[3] = key[2];
  addr.addr[2] = key[3];
  addr.addr[1] = key[4];
  addr.addr[0] = key[5];

  // 31-byte OpenHaystack advertisement payload.
  uint8_t adv[31];
  adv[0]  = 0x1E;        // length: 30 bytes follow
  adv[1]  = 0xFF;        // AD type: manufacturer specific data
  adv[2]  = 0x4C;        // company id: Apple (0x004C), little-endian
  adv[3]  = 0x00;
  adv[4]  = 0x12;        // Apple payload type: offline finding
  adv[5]  = 0x19;        // length of remaining offline-finding payload (25)
  adv[6]  = 0x00;        // status byte
  memcpy(&adv[7], &key[6], 22);  // public key bytes 6..27
  adv[29] = key[0] >> 6;         // top two bits of key[0]
  adv[30] = 0x00;                // hint

  if (!_started) {
    // First time: bring up the stack (shared one-shot guard - see NRF52Board) and configure.
    if (!NRF52Board::beginBluefruitOnce()) return;
    Bluefruit.setTxPower(_tx_dbm);
    Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
    Bluefruit.Advertising.restartOnDisconnect(false);
    Bluefruit.Advertising.setInterval(FINDMY_ADV_INTERVAL, FINDMY_ADV_INTERVAL);
    Bluefruit.Advertising.setFastTimeout(0);
  } else {
    // Rotating to a new key: stop the current advert before re-arming.
    Bluefruit.Advertising.stop();
  }

  sd_ble_gap_addr_set(&addr);
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.setData(adv, sizeof(adv));

  if (Bluefruit.Advertising.start(0)) _started = true;  // 0 = advertise forever
}

void FindMyBeacon::begin(mesh::RTCClock& clock, int8_t tx_dbm) {
  _clock = &clock;
  _now = clock.getCurrentTime();
  if (_started) return;
  _tx_dbm = tx_dbm;

  load();
  if (!_enabled || _count == 0) return;

  _cur_slot = (_now >= FINDMY_MIN_VALID_TIME) ? (uint16_t)((_now / 86400UL) % _count) : 0;
  startAdvertising(_keys[_cur_slot]);
}

void FindMyBeacon::loop(unsigned long now_millis) {
  if (!_enabled || _count == 0 || !_clock) return;

  // Throttle: consult the clock at most once per FINDMY_CHECK_INTERVAL_MS (millis() is free;
  // reading the RTC is not). Unsigned subtraction handles millis() wraparound.
  if (_last_check != 0 && (now_millis - _last_check) < FINDMY_CHECK_INTERVAL_MS) return;
  _last_check = now_millis;

  _now = _clock->getCurrentTime();
  if (_now < FINDMY_MIN_VALID_TIME) return;     // clock not set yet; keep current slot

  uint16_t slot = (uint16_t)((_now / 86400UL) % _count);
  if (_started && slot == _cur_slot) return;    // no day rollover

  _cur_slot = slot;
  startAdvertising(_keys[slot]);
}

void FindMyBeacon::stop() {
  if (_started) {
    Bluefruit.Advertising.stop();
    _started = false;
  }
}

// Decode a base64 advertising key into _keys[slot]. Returns true on success (28 bytes).
static bool decode_key(const char* b64, uint8_t out[28], char* reply) {
  uint8_t decoded[40];
  unsigned int len = decode_base64((unsigned char *)b64, strlen(b64), (unsigned char *)decoded);
  if (len != 28) { sprintf(reply, "Error: decoded %u bytes, expected 28", len); return false; }
  memcpy(out, decoded, 28);
  return true;
}

bool FindMyBeacon::handleCommand(uint32_t sender_timestamp, const char* command, char* reply) {
  if (memcmp(command, "set findmy.add ", 15) == 0) {
    // append in the next free slot
    if (_count >= FINDMY_MAX_KEYS) { sprintf(reply, "Error: full (%d keys)", FINDMY_MAX_KEYS); return true; }
    if (!decode_key(&command[15], _keys[_count], reply)) return true;
    _count++;
    save();
    sprintf(reply, "OK - appended slot %u (%u keys)", _count - 1, _count);
    return true;
  }
  if (memcmp(command, "set findmy.key ", 15) == 0) {
    // set findmy.key <index> <base64>
    const char* p = &command[15];
    char* end;
    long index = strtol(p, &end, 10);
    if (end == p || *end != ' ') { strcpy(reply, "Error: usage: set findmy.key <index> <base64>"); return true; }
    if (index < 0 || index >= FINDMY_MAX_KEYS) { sprintf(reply, "Error: index 0..%d", FINDMY_MAX_KEYS - 1); return true; }
    if (index > _count) { sprintf(reply, "Error: gap - next free slot is %u", _count); return true; }

    const char* b64 = end + 1;
    while (*b64 == ' ') b64++;
    if (!decode_key(b64, _keys[index], reply)) return true;
    if (index == _count) _count++;   // append
    save();
    sprintf(reply, "OK - slot %ld set (%u keys)", index, _count);
    return true;
  }
  if (memcmp(command, "set findmy.clear", 16) == 0) {
    _count = 0;
    _enabled = 0;
    memset(_keys, 0, sizeof(_keys));
    save();
    strcpy(reply, "OK - cleared, reboot to apply");
    return true;
  }
  if (memcmp(command, "set findmy ", 11) == 0) {
    _enabled = memcmp(&command[11], "on", 2) == 0;
    save();
    if (_clock) _now = _clock->getCurrentTime();   // fresh time for the warning below
    if (_enabled && _count > 1 && _now < FINDMY_MIN_VALID_TIME) {
      // rotation needs a real clock; warn rather than silently sticking on slot 0
      strcpy(reply, "OK - on, reboot to apply. WARNING: clock not set - set the node time or "
                    "keys will not rotate (stays on slot 0)");
    } else {
      strcpy(reply, _enabled ? "OK - on, reboot to apply" : "OK - off, reboot to apply");
    }
    return true;
  }
  if (memcmp(command, "get findmy.keys", 15) == 0) {
    // dump all (public) keys to the local serial console; too large for a mesh reply
    if (sender_timestamp != 0) { strcpy(reply, "Error: serial console only"); return true; }
    Serial.printf("FindMy keys (%u):\n", _count);
    for (uint16_t i = 0; i < _count; i++) {
      char b64[44];
      unsigned int n = encode_base64(_keys[i], 28, (unsigned char *)b64);
      b64[n] = 0;
      Serial.printf("%u: %s\n", i, b64);
    }
    reply[0] = 0;
    return true;
  }
  if (memcmp(command, "get findmy.key ", 15) == 0) {
    long index = strtol(&command[15], nullptr, 10);
    if (index < 0 || index >= _count) { sprintf(reply, "Error: index 0..%d", _count ? _count - 1 : 0); return true; }
    char b64[44];
    unsigned int n = encode_base64(_keys[index], 28, (unsigned char *)b64);
    b64[n] = 0;
    sprintf(reply, "> %ld: %s", index, b64);
    return true;
  }
  if (memcmp(command, "get findmy", 10) == 0) {
    if (_clock) _now = _clock->getCurrentTime();   // fresh time for the clock state below
    if (_count == 0) {
      sprintf(reply, "> %s, 0 keys", _enabled ? "on" : "off");
    } else {
      const uint8_t* k = _keys[_cur_slot];
      // derived static-random MAC is key[0]|0xC0 : key[1] : ... : key[5]
      int n = sprintf(reply, "> %s, %u keys, slot %u, mac %02X:%02X:%02X:%02X:%02X:%02X",
                      _enabled ? "on" : "off", _count, _cur_slot,
                      k[0] | 0xC0, k[1], k[2], k[3], k[4], k[5]);
      // for a rotating set, report whether the clock is set (rotation depends on it)
      if (_count > 1) {
        sprintf(reply + n, ", %s", (_now >= FINDMY_MIN_VALID_TIME)
                ? "clock set" : "CLOCK NOT SET - no rotation");
      }
    }
    return true;
  }
  return false;
}

#endif
