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
  return commit_big(c);
}
#endif  // ASR650X_PLATFORM
