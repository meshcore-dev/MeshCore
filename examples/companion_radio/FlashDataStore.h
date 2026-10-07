#pragma once
#include "NodePrefs.h"

#include <Identity.h>
#include <helpers/ChannelDetails.h>
#include <helpers/ContactInfo.h>
#include <helpers/asr650x/FlashRecordStore.h>
#include <helpers/asr650x/FlashRowBackend.h>

class DataStoreHost {
public:
  virtual bool onContactLoaded(const ContactInfo &contact) = 0;
  virtual bool getContactForSave(uint32_t idx, ContactInfo &contact) = 0;
  virtual bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails &ch) = 0;
  virtual bool getChannelForSave(uint8_t channel_idx, ChannelDetails &ch) = 0;
};

// Same surface MyMesh uses from MeshCore's file-based DataStore. Identity + name + radio params go to
// SFLASH rows 0/1 (FlashRecord, A/B). Up to 3 contacts added or edited by the app are pinned in SFLASH row 2
// (PinnedContacts.h); all other contacts, channels and blobs are RAM-only.
class DataStore {
  FlashRowBackend _be;
  asr650x::PersistStore<FlashRowBackend> _store;
  bool commitBig(
      const asr650x::PersistState &c); // runs the flash commit on the crypto stack (main stack is only 2 KB)
public:
  DataStore() : _store(_be) {}
  void begin();
  bool formatFileSystem(); // factory reset: erases identity + prefs record and the pinned contacts
  bool loadMainIdentity(mesh::LocalIdentity &identity);
  bool saveMainIdentity(const mesh::LocalIdentity &identity);
  void loadPrefs(NodePrefs &prefs);
  bool savePrefs(NodePrefs &prefs);
  void loadContacts(DataStoreHost *host); // pinned contacts back into the table at boot
  void saveContacts(DataStoreHost *host,
                    bool (*filter)(const ContactInfo &c) = NULL); // refreshes pinned copies
  void pinContact(const ContactInfo &c);                          // the app added or edited this contact
  void unpinContact(const uint8_t *pub_key);                      // the app removed it
  void loadChannels(DataStoreHost *) {}
  void saveChannels(DataStoreHost *) {}
  uint8_t getBlobByKey(const uint8_t[], int, uint8_t[]) { return 0; }
  bool putBlobByKey(const uint8_t[], int, const uint8_t[], uint8_t) { return false; }
  bool deleteBlobByKey(const uint8_t[], int) { return false; }
  uint32_t getStorageUsedKb() const { return 0; }
  uint32_t getStorageTotalKb() const { return 0; }
};
