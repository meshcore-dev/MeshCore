#if defined(ASR650X_PLATFORM)   // the other boards use DataStore.cpp (filesystem)
#include "FlashDataStore.h"
#include "target.h"
#include <helpers/asr650x/BigStack.h>

namespace {
struct CommitArgs { asr650x::PersistStore<FlashRowBackend>* store; const asr650x::PersistState* cand; bool ok; };
void run_commit(void* p) { CommitArgs* a = (CommitArgs*)p; a->ok = a->store->commit(*a->cand); }
}

bool DataStore::commit_big(const asr650x::PersistState& c) {
  CommitArgs a = { &_store, &c, false };
  asr650x_call_on_big_stack(run_commit, &a);
  return a.ok;
}

void DataStore::begin() {
  _store.begin();
  asr650x_set_tx_allowed(_store.state().has_identity);   // never transmit as the unprovisioned placeholder
}

bool DataStore::formatFileSystem() {
  asr650x::PersistState empty;
  memset(&empty, 0, sizeof(empty));
  bool ok = commit_big(empty);
  if (ok) asr650x_set_tx_allowed(false);
  return ok;
}

bool DataStore::loadMainIdentity(mesh::LocalIdentity& identity) {
  const asr650x::PersistState& s = _store.state();
  if (!s.has_identity) return false;
  uint8_t raw[96];
  memcpy(raw, s.prv, 64);
  memcpy(raw + 64, s.pub, 32);
  identity.readFrom(raw, sizeof(raw));
  return true;
}

bool DataStore::saveMainIdentity(const mesh::LocalIdentity& identity) {
  if (asr650x_identity_is_placeholder(identity)) {                   // never persist the placeholder ...
    asr650x_set_tx_allowed(false);                                   // ... and never transmit as it (even if a real key was loaded before)
    return true;
  }
  mesh::LocalIdentity copy = identity;
  uint8_t raw[96];
  if (copy.writeTo(raw, sizeof(raw)) != sizeof(raw)) return false;
  asr650x::PersistState c = _store.state();
  c.has_identity = true;
  memcpy(c.prv, raw, 64);
  memcpy(c.pub, raw + 64, 32);
  bool ok = commit_big(c);
  if (ok) asr650x_set_tx_allowed(true);
  return ok;
}

void DataStore::loadPrefs(NodePrefs& prefs) {
  const asr650x::PersistState& s = _store.state();
  if (!s.has_prefs) { prefs.gps_enabled = 1; return; }          // default: GPS on
  prefs.gps_enabled = s.gps_off ? 0 : 1;
  prefs.gps_interval = s.gps_interval;
  strncpy(prefs.node_name, s.node_name, sizeof(prefs.node_name) - 1);
  prefs.node_name[sizeof(prefs.node_name) - 1] = 0;
  prefs.freq = s.freq; prefs.bw = s.bw;
  prefs.sf = s.sf; prefs.cr = s.cr; prefs.tx_power_dbm = s.tx_power_dbm;
  if (!s.has_ext) return;                                        // record from an older build: keep the defaults
  prefs.airtime_factor = s.airtime_factor;
  prefs.rx_delay_base = s.rx_delay_base;
  prefs.tx_delay_factor = s.tx_delay_factor;
  prefs.direct_tx_delay_factor = s.direct_tx_delay_factor;
  prefs.multi_acks = s.multi_acks;
  prefs.manual_add_contacts = s.manual_add_contacts;
  prefs.telemetry_mode_base = s.telemetry_modes & 0x03;
  prefs.telemetry_mode_loc = (s.telemetry_modes >> 2) & 0x03;
  prefs.telemetry_mode_env = (s.telemetry_modes >> 4) & 0x03;
  prefs.advert_loc_policy = s.advert_loc_policy;
  prefs.autoadd_config = s.autoadd_config;
  prefs.autoadd_max_hops = s.autoadd_max_hops;
  prefs.path_hash_mode = s.path_hash_mode;
  prefs.tz_offset = s.tz_offset;
  prefs.cad_enabled = s.cad_enabled;
  prefs.interference_threshold = s.interference_threshold;
  prefs.agc_reset_interval = s.agc_reset_interval;
  prefs.rx_boosted_gain = s.rx_boosted_gain;
}

bool DataStore::savePrefs(NodePrefs& prefs) {
  asr650x::PersistState c = _store.state();
  c.has_prefs = true;
  c.gps_off = prefs.gps_enabled == 0;
  c.gps_interval = prefs.gps_interval;
  strncpy(c.node_name, prefs.node_name, sizeof(c.node_name) - 1);
  c.node_name[sizeof(c.node_name) - 1] = 0;
  c.freq = prefs.freq; c.bw = prefs.bw;
  c.sf = prefs.sf; c.cr = prefs.cr; c.tx_power_dbm = prefs.tx_power_dbm;
  c.has_ext = true;
  c.airtime_factor = prefs.airtime_factor;
  c.rx_delay_base = prefs.rx_delay_base;
  c.tx_delay_factor = prefs.tx_delay_factor;
  c.direct_tx_delay_factor = prefs.direct_tx_delay_factor;
  c.multi_acks = prefs.multi_acks;
  c.manual_add_contacts = prefs.manual_add_contacts;
  c.telemetry_modes = (uint8_t)((prefs.telemetry_mode_base & 0x03) | ((prefs.telemetry_mode_loc & 0x03) << 2) |
                                ((prefs.telemetry_mode_env & 0x03) << 4));
  c.advert_loc_policy = prefs.advert_loc_policy;
  c.autoadd_config = prefs.autoadd_config;
  c.autoadd_max_hops = prefs.autoadd_max_hops;
  c.path_hash_mode = prefs.path_hash_mode;
  c.tz_offset = prefs.tz_offset;
  c.cad_enabled = prefs.cad_enabled;
  c.interference_threshold = prefs.interference_threshold;
  c.agc_reset_interval = prefs.agc_reset_interval;
  c.rx_boosted_gain = prefs.rx_boosted_gain;
  return commit_big(c);
}
#endif  // ASR650X_PLATFORM
