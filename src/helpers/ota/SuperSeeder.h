#pragma once

#if defined(OTA_SUPERSEEDER)

#include <stdint.h>
#include "OtaManager.h"
#include "MotaSourceSeeder.h"
#include "SeederAllowlist.h"
#include "SeederMotaStore.h"

namespace mesh {
namespace ota {

class OtaContext;

// External-FS superseeder: promiscuous catalog discovery + capture *deltas*
// (optionally filtered by a runtime target allowlist; empty = all targets)
// to SD or QSPI LittleFS. Full snapshots are never stored.
class SuperSeeder {
public:
  void begin(OtaContext& ctx);
  void loop();
  void refreshSource();  // re-scan FS after allowlist change

  bool active() const { return _active; }
  bool mounted() const { return _mounted; }
  uint8_t fileCount() const { return _source.cachedCount(); }
  uint32_t totalBytes() const { return _source.cachedTotalBytes(); }
  bool capturing() const { return _capturing; }

  MotaSourceSeeder& source() { return _source; }
  SeederMotaStore&  store()  { return _store; }

private:
  void finishCapture(bool ok);
  bool pickNext(uint8_t mid[4], uint32_t& target);

  OtaContext*      _ctx = nullptr;
  OtaManager*      _mgr = nullptr;
  OtaStore*        _flash_store = nullptr;
  SeederAllowlist* _allow = nullptr;
  MotaSourceSeeder _source;
  SeederMotaStore  _store;
  bool             _active = false;
  bool             _mounted = false;
  bool             _capturing = false;
  uint32_t         _fail_cooldown_until = 0;
  uint8_t          _fail_mid[4] = {0};
};

}  // namespace ota
}  // namespace mesh

#endif
