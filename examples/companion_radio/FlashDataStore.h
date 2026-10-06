#pragma once
#include <Identity.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>
#include "NodePrefs.h"
#include <helpers/asr650x/FlashRowBackend.h>
#include <helpers/asr650x/FlashRecordStore.h>

class DataStoreHost {
public:
  virtual bool onContactLoaded(const ContactInfo& contact) = 0;
  virtual bool getContactForSave(uint32_t idx, ContactInfo& contact) = 0;
  virtual bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) = 0;
  virtual bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) = 0;
};

// Same surface MyMesh uses from MeshCore's file-based DataStore. Identity + name + radio params go to
// EEPROM (CcPersist record); contacts/channels/blobs are RAM-only by design (M1 scope).
class DataStore {
  FlashRowBackend _be;
  asr650x::PersistStore<FlashRowBackend> _store;
  bool commit_big(const asr650x::PersistState& c);   // runs the flash commit on the crypto stack (main stack is only 2 KB)
public:
  DataStore() : _store(_be) {}
  void begin();
  bool formatFileSystem();   // factory reset: erases identity + prefs record
  bool loadMainIdentity(mesh::LocalIdentity& identity);
  bool saveMainIdentity(const mesh::LocalIdentity& identity);
  void loadPrefs(NodePrefs& prefs);
  bool savePrefs(NodePrefs& prefs);
  void loadContacts(DataStoreHost*) {}
  void saveContacts(DataStoreHost*, bool (*filter)(const ContactInfo& c) = NULL) {}
  void loadChannels(DataStoreHost*) {}
  void saveChannels(DataStoreHost*) {}
  uint8_t getBlobByKey(const uint8_t[], int, uint8_t[]) { return 0; }
  bool putBlobByKey(const uint8_t[], int, const uint8_t[], uint8_t) { return false; }
  bool deleteBlobByKey(const uint8_t[], int) { return false; }
  uint32_t getStorageUsedKb() const { return 0; }
  uint32_t getStorageTotalKb() const { return 0; }
};
