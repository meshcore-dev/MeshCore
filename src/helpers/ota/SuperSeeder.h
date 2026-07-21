#pragma once

#if defined(OTA_SD_SEEDER)

#include <stdint.h>
#include "OtaManager.h"
#include "MotaSourceSd.h"
#include "SdMotaStore.h"

namespace mesh {
namespace ota {

class OtaContext;

// SD superseeder orchestrator: promiscuous catalog discovery + capture unknown mids to SD.
class SuperSeeder {
public:
  void begin(OtaContext& ctx);
  void loop();

  bool active() const { return _active; }
  bool mounted() const { return _mounted; }
  uint8_t fileCount() const { return _source.cachedCount(); }
  uint32_t totalBytes() const { return _source.cachedTotalBytes(); }
  bool capturing() const { return _capturing; }

  MotaSourceSd& source() { return _source; }
  SdMotaStore&  store()  { return _store; }

private:
  void finishCapture(bool ok);
  bool pickNext(uint8_t mid[4], uint32_t& target);

  OtaContext*   _ctx = nullptr;
  OtaManager*   _mgr = nullptr;
  OtaStore*     _flash_store = nullptr;
  MotaSourceSd  _source;
  SdMotaStore   _store;
  bool          _active = false;
  bool          _mounted = false;
  bool          _capturing = false;
  uint32_t      _fail_cooldown_until = 0;
  uint8_t       _fail_mid[4] = {0};
};

} // namespace ota
} // namespace mesh

#endif
