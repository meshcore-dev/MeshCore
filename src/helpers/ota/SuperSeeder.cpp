#include "SuperSeeder.h"

#if defined(OTA_SUPERSEEDER)

#include "OtaContext.h"
#include "SeederAllowlist.h"
#include "SeederFs.h"
#include <Arduino.h>
#include <string.h>

namespace mesh {
namespace ota {

void SuperSeeder::begin(OtaContext& ctx) {
  _ctx = &ctx;
  _mgr = &ctx.manager;
  _flash_store = &ctx.fetch_store;
  _allow = &ctx.seeder_allow;
  _source.setAllowlist(_allow);
  _mounted = SeederFs::instance().mount();
  if (!_mounted) return;
  _source.refresh();
  if (!_mgr->add_source(&_source)) return;
  _mgr->set_promiscuous(true);
  _mgr->set_capture_mode(true);  // accept foreign codecs while staging for re-seed
  _active = true;
}

void SuperSeeder::refreshSource() {
  if (!_active || !_mgr) return;
  _source.refresh();
  _mgr->refresh_sources();
  _mgr->announce();
}

bool SuperSeeder::pickNext(uint8_t mid[4], uint32_t& target) {
  if (!_mgr || !_mounted || !_allow) return false;
  for (uint8_t i = 0; i < _mgr->catalogCount(); i++) {
    const OtaManager::CatRow* row = _mgr->catalogRow(i);
    if (!row) continue;
    if (!ota_seeder_admit(*_allow, row->target_id, row->codec, row->flags)) continue;
    if (_source.hasMid(row->mid)) continue;
    if (_fail_cooldown_until && memcmp(_fail_mid, row->mid, 4) == 0) continue;
    memcpy(mid, row->mid, 4);
    target = row->target_id;
    return true;
  }
  return false;
}

void SuperSeeder::finishCapture(bool ok) {
  if (!_ctx || !_mgr) return;
  if (!ok) {
    uint8_t mid[4];
    memcpy(mid, _mgr->fetchManifestId(), 4);
    char part[48];
    SeederFs::instance().midPath(mid, part, sizeof part, ".mota.part");
    SeederFs::instance().removeFile(part);
    memcpy(_fail_mid, mid, 4);
    _fail_cooldown_until = millis() + 60000UL;
  }
  _store.clear();
  _mgr->reset_session();
  _mgr->want_mid(nullptr);
  _mgr->want(0);
  _mgr->set_fetch_store(_flash_store);
  _capturing = false;
  if (ok) {
    _source.refresh();
    _mgr->refresh_sources();
    _mgr->announce();
  }
}

void SuperSeeder::loop() {
  if (!_active || !_ctx || !_mgr) return;

  if (_fail_cooldown_until && (int32_t)(millis() - _fail_cooldown_until) >= 0)
    _fail_cooldown_until = 0;

  OtaManager::FetchState fs = _mgr->fetchState();

  if (_capturing) {
    if (fs == OtaManager::COMPLETE) {
      finishCapture(true);
      return;
    }
    if (fs == OtaManager::FAILED || fs == OtaManager::PAUSED) {
      finishCapture(false);
      return;
    }
    return;
  }

  if (fs != OtaManager::IDLE) return;

  uint8_t mid[4];
  uint32_t target = 0;
  if (!pickNext(mid, target)) return;

  _store.set_mid(mid);
  _mgr->set_fetch_store(&_store);
  _mgr->want(0);
  _mgr->want_mid(mid);
  _mgr->pull(mid, target, false);
  _capturing = true;
}

}  // namespace ota
}  // namespace mesh

#endif
