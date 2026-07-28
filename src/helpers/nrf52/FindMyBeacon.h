#pragma once

#include <stdint.h>

namespace mesh { class RTCClock; }

// Maximum number of daily rotation slots (one per day; cycles every `count` days).
#ifndef FINDMY_MAX_KEYS
#define FINDMY_MAX_KEYS 365
#endif

// Apple FindMy / OpenHaystack locator beacon for nRF52 (Adafruit Bluefruit).
//
// Advertises a static, non-connectable OpenHaystack payload derived from a 28-byte
// advertising public key. The matching private key is held off-device (in the user's
// OpenHaystack / macless-haystack setup) and is required to actually locate the device.
//
// Supports daily key rotation: up to FINDMY_MAX_KEYS public keys are stored, and the active
// slot is chosen from the clock as (now_utc / 86400) % count. With count == 1 this reduces to
// a single static key; with more keys it rotates daily and cycles every `count` days, the way an
// AirTag rotates (the server, holding the matching private keys, derives the same per-day slot).
//
// The algorithm is ported from https://github.com/pix/heystack-nrf5x (nRF5 SDK) and
// reimplemented here on the Bluefruit advertising API used by MeshCore.
//
// Self-contained: persists its own config to "/findmy" and parses its own "set/get findmy" CLI
// commands, so it needs no changes to the shared NodePrefs/CommonCLI code.
//
// Intended for always-on roles (repeater/sensor) where BLE is otherwise unused. It is not meant
// to run alongside the phone-companion firmware, which needs BLE for its own link.
class FindMyBeacon {
  bool _started = false;
  uint8_t _enabled = 0;
  uint16_t _count = 0;          // number of provisioned keys (0..FINDMY_MAX_KEYS)
  uint16_t _cur_slot = 0;       // currently advertised slot
  uint32_t _now = 0;            // last UTC time read from the clock
  unsigned long _last_check = 0;// millis() of the last clock check (rotation throttle)
  mesh::RTCClock* _clock = nullptr;
  int8_t _tx_dbm = 4;
  uint8_t _keys[FINDMY_MAX_KEYS][28] = {{0}};

  bool load();                  // read "/findmy"
  void save();                  // write "/findmy"
  void startAdvertising(const uint8_t key[28]);  // (re)advertise the given key

public:
  // Load persisted config and, if enabled with keys, start advertising the slot for the current
  // time. Call once at boot after the internal filesystem is mounted. The clock is read here and
  // periodically in loop(); if it is not set yet, slot 0 is used until it is.
  void begin(mesh::RTCClock& clock, int8_t tx_dbm = 4);

  // Call every main-loop iteration with millis(). To avoid a per-loop RTC read (an I2C
  // transaction on boards with a hardware RTC), it only consults the clock about once an hour
  // and rotates the key at day boundaries - daily rotation needs no finer precision.
  void loop(unsigned long now_millis);

  void stop();
  bool isRunning() const { return _started; }

  // Handle the findmy CLI commands (works over serial and remote admin):
  //   set findmy.add <base64>           append a key in the next free slot
  //   set findmy.key <index> <base64>   set/replace slot <index> (append if index == count)
  //   set findmy.clear                  erase all keys
  //   set findmy on|off                 enable/disable
  //   get findmy                        status (enabled, key count, current slot, MAC)
  //   get findmy.key <index>            print the (public) advertisement key for a slot
  //   get findmy.keys                   list all keys to serial (local console only)
  // sender_timestamp is 0 for the local serial console, non-zero for remote admin.
  // Returns true if the command was recognised (and reply filled), false otherwise.
  bool handleCommand(uint32_t sender_timestamp, const char* command, char* reply);
};

// Single shared instance (defined in FindMyBeacon.cpp).
extern FindMyBeacon findmy_beacon;
