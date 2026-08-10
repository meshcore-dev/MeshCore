#include <Arduino.h>
#include "DataStore.h"

#include <helpers/MigrateContactsToChunks.h> // remove when we have migrated to chunked contacts

#if defined(EXTRAFS) || defined(QSPIFLASH)
  #define MAX_BLOBRECS 40
#else
  #define MAX_BLOBRECS 20
#endif

#define CONTACTS_DEBOUNCE_MS   5000   // time to wait for additional changes before writing dirty contacts
#define CONTACTS_MAX_DIRTY_MS 30000   // maximum time to wait before writing dirty contacts

DataStore::DataStore(FILESYSTEM& fs, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(nullptr), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}

#if defined(EXTRAFS) || defined(QSPIFLASH)
DataStore::DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(&fsExtra), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fsExtra, "", fs)
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}
#endif

static File openWrite(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(filename);
  return fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "w");
#else
  return fs->open(filename, "w", true);
#endif
}

static bool renameFile(FILESYSTEM* fs, const char* oldname, const char* newname) {
  // LittleFS supports atomic rename, but SPIFFS does not.
  #if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM) || defined(RP2040_PLATFORM)
    return fs->rename(oldname, newname);
  #else
    if (fs->exists(newname)) {
      fs->remove(newname);
    }
    return fs->rename(oldname, newname);
  #endif
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  static uint32_t _storageFSTotalBlocks = 0;
#endif

void DataStore::begin() {
#if defined(RP2040_PLATFORM)
  identity_store.begin();
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _storageFSTotalBlocks = _getStorageFS()->_getFS()->cfg->block_count;
  checkAdvBlobFile();
  #if defined(EXTRAFS) || defined(QSPIFLASH)
  migrateToSecondaryFS();
  #endif
#else
  // init 'blob store' support
  _fs->mkdir("/bl");
#endif
}

#if defined(ESP32)
  #include <SPIFFS.h>
  #include <nvs_flash.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #if defined(QSPIFLASH)
    #include <CustomLFS2_QSPIFlash.h>
  #elif defined(EXTRAFS)
    #include <CustomLFS2.h>
  #else 
    #include <InternalFileSystem.h>
  #endif
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
int _countLfsBlock(void *p, lfs_block_t block){
      if (block > _storageFSTotalBlocks) {
        MESH_DEBUG_PRINTLN("ERROR: Block %d exceeds filesystem bounds - CORRUPTION DETECTED!", block);
        return LFS_ERR_CORRUPT;  // return error to abort lfs_traverse() gracefully
    }
  lfs_size_t *size = (lfs_size_t*) p;
  *size += 1;
    return 0;
}

lfs_ssize_t _getLfsUsedBlockCount(FILESYSTEM* fs) {
  lfs_size_t size = 0;
  int err = lfs_fs_traverse(fs->_getFS(), _countLfsBlock, &size);
  if (err) {
    MESH_DEBUG_PRINTLN("ERROR: lfs_traverse() error: %d", err);
    lfs_fs_mkconsistent(fs->_getFS());
    return 0;
  }
  return size;
}
#endif

uint32_t DataStore::getStorageUsedKb() const {
#if defined(ESP32)
  return SPIFFS.usedBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.usedBytes = 0;
  _fs->info(info);
  return info.usedBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getStorageFS()->_getFS()->cfg;
  int usedBlockCount = _getLfsUsedBlockCount(_getStorageFS());
  int usedBytes = config->block_size * usedBlockCount;
  return usedBytes / 1024;
#else
  return 0;
#endif
}

uint32_t DataStore::getStorageTotalKb() const {
#if defined(ESP32)
  return SPIFFS.totalBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.totalBytes = 0;
  _fs->info(info);
  return info.totalBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getStorageFS()->_getFS()->cfg;
  int totalBytes = config->block_size * config->block_count;
  return totalBytes / 1024;
#else
  return 0;
#endif
}

File DataStore::openRead(const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return _fs->open(filename, "r");
#else
  return _fs->open(filename, "r", false);
#endif
}

File DataStore::openRead(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "r");
#else
  return fs->open(filename, "r", false);
#endif
}

bool DataStore::removeFile(const char* filename) {
  return _fs->remove(filename);
}

bool DataStore::removeFile(FILESYSTEM* fs, const char* filename) {
  return fs->remove(filename);
}

bool DataStore::relocateFile(FILESYSTEM* src, FILESYSTEM* dst, const char* filename) {
  if (!src->exists(filename)) return false;

  char tempfile[24];
  snprintf(tempfile, sizeof(tempfile), "%s.tmp", filename);
  File oldFile = openRead(src, filename);
  File newFile = openWrite(dst, tempfile);
  if (!oldFile || !newFile) {
    if (oldFile) oldFile.close();
    if (newFile) newFile.close();
    return false;
  }

  bool ok = true;
  uint8_t buf[64];
  int n;
  while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
    if (newFile.write(buf, n) != n) { ok = false; break; };
  }
  if (n < 0) ok = false;

  oldFile.close();
  newFile.close();

  if (ok) {
    ok = renameFile(dst, tempfile, filename);
    if (ok) {
      ok = src->remove(filename);
    }
    if (!ok) {
      dst->remove(tempfile);
    }
  }
  MESH_DEBUG_PRINTLN("Relocate %s: %s", filename, ok ? "success" : "failed");

  return ok;
}

bool DataStore::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (_fsExtra == nullptr) {
    return _fs->format();
  } else {
    return _fs->format() && _fsExtra->format();
  }
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  bool fs_success = ((fs::SPIFFSFS *)_fs)->format();
  esp_err_t nvs_err = nvs_flash_erase(); // no need to reinit, will be done by reboot
  return fs_success && (nvs_err == ESP_OK);
#else
  #error "need to implement format()"
#endif
}

bool DataStore::loadMainIdentity(mesh::LocalIdentity &identity) {
  return identity_store.load("_main", identity);
}

bool DataStore::saveMainIdentity(const mesh::LocalIdentity &identity) {
  return identity_store.save("_main", identity);
}

void DataStore::loadPrefs(NodePrefs& prefs) {
  if (_getStorageFS()->exists("/prefs.json")) {
    File file = openRead(_getStorageFS(), "/prefs.json");
    if (file) {
      prefs.loadSerial(file);   // new Serial prefs
      file.close();
    }
  } else if (_getStorageFS()->exists("/new_prefs")) {
    loadPrefsInt("/new_prefs", prefs);
    if (savePrefs(prefs) ) {                // save to new format
      //_getStorageFS()->remove("/new_prefs"); // remove old
    }
  }
}

void DataStore::loadPrefsInt(const char *filename, NodePrefs& _prefs) {
  File file = openRead(_getStorageFS(), filename);
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs.airtime_factor, sizeof(float));                           // 0
    file.read((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));                      // 4
    file.read(pad, 4);                                                                     // 36
    file.read((uint8_t *)&_prefs.node_lat, sizeof(_prefs.node_lat));                       // 40
    file.read((uint8_t *)&_prefs.node_lon, sizeof(_prefs.node_lon));                       // 48
    file.read((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));                               // 56
    file.read((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));                                   // 60
    file.read((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));                                   // 61
    file.read((uint8_t *)&_prefs._client_repeat, sizeof(_prefs._client_repeat));             // 62
    file.read((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)); // 63
    file.read((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));                                   // 64
    file.read((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));               // 68
    file.read((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)); // 69
    file.read((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));   // 70
    file.read((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));   // 71
    file.read((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));             // 72
    file.read((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));     // 76
    file.read((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));                   // 77
    file.read((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode));           // 78
    file.read(pad, 1);                                                                     // 79
    file.read((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));                         // 80
    file.read((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));               // 84
    file.read((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));                 // 85
    file.read((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));               // 86
    file.read((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));           // 87
    file.read((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops));       // 88
    file.read((uint8_t *)&_prefs.rx_boosted_gain, sizeof(_prefs.rx_boosted_gain));         // 89
    file.read((uint8_t *)_prefs.default_scope_name, sizeof(_prefs.default_scope_name));    // 90
    file.read((uint8_t *)_prefs.default_scope_key, sizeof(_prefs.default_scope_key));     // 121

    // migrate old fields
    _prefs.setRepeatEn(_prefs._client_repeat != 0);

    file.close();
  }
}

bool DataStore::savePrefs(NodePrefs& _prefs) {
  constexpr char filename[] = "/prefs.json";
  char tempname[sizeof(filename) + 4];
  snprintf(tempname, sizeof(tempname), "%s.tmp", filename);

  File file = openWrite(_getStorageFS(), tempname);
  if (!file) return false;

  bool writeOk = _prefs.saveSerial(file);
  file.close();

  return writeOk && renameFile(_getStorageFS(), tempname, filename);
}

void DataStore::allocateChunkSlot(ContactInfo& contact)
{
    for (uint8_t chunk = 0; chunk < MAX_CHUNKS; chunk++) {
        if (_chunk_free_slots[chunk] == 0) continue;
        for (uint8_t slot = 0; slot < SLOTS_PER_CHUNK; slot++) {
            if (_chunk_free_slots[chunk] & (1 << slot)) {
              MESH_DEBUG_PRINTLN("allocateChunkSlot: chose slot, chunk %d slot %d", chunk, slot);
                contact.chunk_index = chunk;
                contact.slot_index = slot;
                _chunk_free_slots[chunk] &= ~(1 << slot);
                // _chunks_to_write |= (1 << chunk); // replaced by markContactDirty()
                markContactDirty(contact);
                MESH_DEBUG_PRINTLN("allocateChunkSlot: assigned chunk=%d slot=%d, free_slots=0x%08X", chunk, slot, _chunk_free_slots[chunk]);
                return;
            }
        }
    }
    MESH_DEBUG_PRINTLN("allocateChunkSlot: no free slots available");
}

void DataStore::releaseChunkSlot(ContactInfo& contact)
{
    if (contact.chunk_index == 0xFF) {
      MESH_DEBUG_PRINTLN("releaseChunkSlot: contact.chunk_index is already 0xFF, returning...");
      return;
    }
    _chunk_free_slots[contact.chunk_index] |= (1 << contact.slot_index);
    _chunks_to_write |= (1 << contact.chunk_index);
    _contactsChanged = millis();
    contact.chunk_index = 0xFF;
    contact.slot_index = 0xFF;
}

void DataStore::markContactDirty(const ContactInfo& contact)
{
    if (contact.chunk_index != 0xFF) {
        _chunks_to_write |= (1 << contact.chunk_index);
        _contactsChanged = millis();
    }
}

bool DataStore::shouldSaveContacts(uint32_t now) {
  if (_saving_contacts || _chunks_to_write == 0) {
      if (_chunks_to_write == 0) {
        _contacts_dirty_since = 0;
        _contactsChanged = 0;
      }
      return false;
  }

  // contacts have changed, set dirty_since if needed
  if (_contactsChanged != 0) {
      if (_contacts_dirty_since == 0) _contacts_dirty_since = _contactsChanged;
  }

  if ((now - _contactsChanged) >= CONTACTS_DEBOUNCE_MS) return true;
  if ((now - _contacts_dirty_since) >= CONTACTS_MAX_DIRTY_MS) return true;

  return false;
}


void DataStore::loadContacts(DataStoreHost* host) {
  _contactsChanged = 0;
  _chunks_to_write = 0;
  _contacts_dirty_since = 0;
  _saving_contacts = false;

  for (int i = 0; i < MAX_CHUNKS; i++) {
      _chunk_free_slots[i] = 0x01FFFFFF;
  }

  const uint32_t chunkSize = sizeof(ChunkHeader) + (SLOTS_PER_CHUNK * CONTACT_RECORD_SIZE);
  static uint8_t chunkBuf[chunkSize];

  // check for monolithic contacts file on _fs and _fsExtra, migrate it to chunked format
  if (_fs->exists("/contacts3")) {
      if (migrateContactsFromFile(_fs, _getStorageFS(), chunkBuf)) _fs->remove("/contacts3");
  } else if (_getStorageFS()->exists("/contacts3")) {
      if (migrateContactsFromFile(_getStorageFS(), _getStorageFS(), chunkBuf)) _getStorageFS()->remove("/contacts3");
  }


  bool full = false;

  for (uint8_t chunk_idx = 0; chunk_idx < MAX_CHUNKS && !full; chunk_idx++) {
      char filename[20];
      snprintf(filename, sizeof(filename), "/contacts3_%02d", chunk_idx);

      File file = openRead(_getStorageFS(), filename);
      if (!file) continue;

      MESH_DEBUG_PRINTLN("loadContacts: reading contact chunk file %s", filename);
      if (file.read(chunkBuf, chunkSize) != chunkSize) {
          MESH_DEBUG_PRINTLN("Chunk %d: read failed, skipping", chunk_idx);
          file.close();
          continue;
      }
      file.close();

      ChunkHeader* header = (ChunkHeader*)chunkBuf;
      uint8_t* records = chunkBuf + sizeof(ChunkHeader);

      // check header, skip if magic/version/chunk_index don't match
      if (header->magic != CHUNK_MAGIC || header->version != CHUNK_VERSION || header->chunk_index != chunk_idx) {
          MESH_DEBUG_PRINTLN("Chunk %d: invalid header, skipping", chunk_idx);
          continue;
      }

      // sanity check before sha256, bail early if things are off
      uint8_t actual_count = 0;
      for (uint8_t i = 0; i < SLOTS_PER_CHUNK; i++) {
          if (header->tombstones[i] == 0) actual_count++;
      }
      if (actual_count != header->valid_count) {
          MESH_DEBUG_PRINTLN("Chunk %d: valid_count mismatch (%d != %d), skipping",
                              chunk_idx, header->valid_count, actual_count);
          continue;
      }

      // validate sha256 hash
      uint8_t stored_hash[32];
      memcpy(stored_hash, header->sha256, 32);
      memset(header->sha256, 0, 32);
      uint8_t calculated_hash[32];
      mesh::Utils::sha256(calculated_hash, 32, chunkBuf, chunkSize);
      if (memcmp(stored_hash, calculated_hash, 32) != 0) {
          MESH_DEBUG_PRINTLN("Chunk %d: SHA256 validation failed, skipping", chunk_idx);
          continue;
      }

      // load contacts from chunk
      for (uint8_t slot_idx = 0; slot_idx < SLOTS_PER_CHUNK && !full; slot_idx++) {
          if (header->tombstones[slot_idx] != 0) continue; // skip deleted or empty slots

          uint8_t* rec = records + (slot_idx * CONTACT_RECORD_SIZE); // pointer to record in chunk buffer
          ContactInfo c;
          uint8_t pub_key[32];
          int off = 0;

          memcpy(pub_key, rec + off, 32);                    off += 32;
          memcpy(c.name, rec + off, 32);                     off += 32;
          c.type = rec[off++];                               // 1 byte
          c.flags = rec[off++];                              // 1 byte
          off++;                                             // skip, 1 byte unused
          memcpy(&c.sync_since, rec + off, 4);               off += 4;  // was 'reserved'
          c.out_path_len = rec[off++];                       // 1 byte
          memcpy(&c.last_advert_timestamp, rec + off, 4);    off += 4;
          memcpy(c.out_path, rec + off, 64);                 off += 64;
          memcpy(&c.lastmod, rec + off, 4);                  off += 4;
          memcpy(&c.gps_lat, rec + off, 4);                  off += 4;
          memcpy(&c.gps_lon, rec + off, 4);                  off += 4;

          c.id = mesh::Identity(pub_key);
          c.shared_secret_valid = false;
          c.chunk_index = chunk_idx;
          c.slot_index = slot_idx;

          if (host->onContactLoaded(c)) {
              _chunk_free_slots[chunk_idx] &= ~(1 << slot_idx);
          } else {
              full = true;
          }
      }
  }
}

void DataStore::saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c)) {
  if (_chunks_to_write == 0 || _saving_contacts) return;

  _saving_contacts = true;
  uint16_t chunks_snapshot = _chunks_to_write;
  _chunks_to_write = 0;
  _contacts_dirty_since = 0;

  const uint32_t chunkSize = sizeof(ChunkHeader) + (SLOTS_PER_CHUNK * CONTACT_RECORD_SIZE);
  static uint8_t chunkBuf[chunkSize];

  for (uint8_t chunk_idx = 0; chunk_idx < MAX_CHUNKS; chunk_idx++) {
    if (!(chunks_snapshot & (1 << chunk_idx))) continue;
    MESH_DEBUG_PRINTLN("saveContacts(): chunk_idx=%d, chunks_snapshot=0x%04X", chunk_idx, chunks_snapshot);

    memset(chunkBuf, 0, chunkSize);

    ChunkHeader* header = (ChunkHeader*)chunkBuf;
    header->magic = CHUNK_MAGIC;
    header->version = CHUNK_VERSION;
    header->chunk_index = chunk_idx;
    header->valid_count = 0;
    memset(header->tombstones, 1, SLOTS_PER_CHUNK);

    uint8_t* records = chunkBuf + sizeof(ChunkHeader);

    // Scan all contacts to find those in this chunk
    uint32_t idx = 0;
    ContactInfo c;
    while (host->getContactForSave(idx, c)) {
      if (filter && !filter(c)) {
        idx++;  // advance to next contact
        continue;
      }
      if (c.chunk_index == chunk_idx) {
        uint8_t* rec = records + (c.slot_index * CONTACT_RECORD_SIZE);
        int off = 0;
        uint8_t unused = 0;

        memcpy(rec + off, c.id.pub_key, 32);                off += 32;
        memcpy(rec + off, c.name, 32);                      off += 32;
        rec[off++] = c.type;
        rec[off++] = c.flags;
        rec[off++] = unused;
        memcpy(rec + off, &c.sync_since, 4);                off += 4;
        rec[off++] = c.out_path_len;
        memcpy(rec + off, &c.last_advert_timestamp, 4);     off += 4;
        memcpy(rec + off, c.out_path, 64);                  off += 64;
        memcpy(rec + off, &c.lastmod, 4);                   off += 4;
        memcpy(rec + off, &c.gps_lat, 4);                   off += 4;
        memcpy(rec + off, &c.gps_lon, 4);                   off += 4;

        header->tombstones[c.slot_index] = 0;
        header->valid_count++;
      }
      idx++;
    }

    // SHA256
    mesh::Utils::sha256(header->sha256, 32, chunkBuf, chunkSize);

    // Atomic write
    char filename[14];
    char tempname[18];
    snprintf(filename, sizeof(filename), "/contacts3_%02d", chunk_idx);
    snprintf(tempname, sizeof(tempname), "/contacts3_%02d.tmp", chunk_idx);

        MESH_DEBUG_PRINTLN("saveContacts: writing %s", tempname);
        File file = openWrite(_getStorageFS(), tempname);
        
        if (file) {
            bool ok = (file.write(chunkBuf, chunkSize) == chunkSize);
            file.close();
            if (ok) {
                MESH_DEBUG_PRINTLN("saveContacts: renaming %s to %s", tempname, filename);
                renameFile(_getStorageFS(), tempname, filename);
            } else {
                _getStorageFS()->remove(tempname);
            }
        }
    }
    _saving_contacts = false;
}

void DataStore::loadChannels(DataStoreHost* host) {
    File file = openRead(_getStorageFS(), "/channels2");
    if (file) {
      bool full = false;
      uint8_t channel_idx = 0;
      while (!full) {
        ChannelDetails ch;
        uint8_t unused[4];

        bool success = (file.read(unused, 4) == 4);
        success = success && (file.read((uint8_t *)ch.name, 32) == 32);
        success = success && (file.read((uint8_t *)ch.channel.secret, 32) == 32);

        if (!success) break; // EOF

        if (host->onChannelLoaded(channel_idx, ch)) {
          channel_idx++;
        } else {
          full = true;
        }
      }
      file.close();
    }
}

void DataStore::saveChannels(DataStoreHost* host) {
  File file = openWrite(_getStorageFS(), "/channels2");
  if (file) {
    uint8_t channel_idx = 0;
    ChannelDetails ch;
    uint8_t unused[4];
    memset(unused, 0, 4);

    while (host->getChannelForSave(channel_idx, ch)) {
      bool success = (file.write(unused, 4) == 4);
      success = success && (file.write((uint8_t *)ch.name, 32) == 32);
      success = success && (file.write((uint8_t *)ch.channel.secret, 32) == 32);

      if (!success) break; // write failed
      channel_idx++;
    }
    file.close();
  }
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)

#define MAX_ADVERT_PKT_LEN   (2 + 32 + PUB_KEY_SIZE + 4 + SIGNATURE_SIZE + MAX_ADVERT_DATA_SIZE)

struct BlobRec {
  uint32_t timestamp;
  uint8_t  key[7];
  uint8_t  len;
  uint8_t  data[MAX_ADVERT_PKT_LEN];
};

void DataStore::checkAdvBlobFile() {
  if (!_getStorageFS()->exists("/adv_blobs")) {
    File file = openWrite(_getStorageFS(), "/adv_blobs");
    if (file) {
      BlobRec zeroes;
      memset(&zeroes, 0, sizeof(zeroes));
      for (int i = 0; i < MAX_BLOBRECS; i++) {     // pre-allocate to fixed size
        file.write((uint8_t *) &zeroes, sizeof(zeroes));
      }
      file.close();
    }
  }
}

void DataStore::migrateToSecondaryFS() {
  // migrate old adv_blobs, contacts, channels, prefs files to secondary FS
  if (!_fsExtra->exists("/adv_blobs")) {
    if (_fs->exists("/adv_blobs")) {
    File oldAdvBlobs = openRead(_fs, "/adv_blobs");
    File newAdvBlobs = openWrite(_fsExtra, "/adv_blobs");

    if (oldAdvBlobs && newAdvBlobs) {
      BlobRec rec;
      size_t count = 0;

      // Copy 20 BlobRecs from old to new
      while (count < 20 && oldAdvBlobs.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
        newAdvBlobs.seek(count * sizeof(BlobRec));
        newAdvBlobs.write((uint8_t *)&rec, sizeof(rec));
        count++;
      }
    }
    if (oldAdvBlobs) oldAdvBlobs.close();
    if (newAdvBlobs) newAdvBlobs.close();
    _fs->remove("/adv_blobs");
    }
  }
  if (!_fsExtra->exists("/contacts3")) {
    if (_fs->exists("/contacts3")) relocateFile(_fs, _fsExtra, "/contacts3");
  }
  if (!_fsExtra->exists("/channels2")) {
    if (_fs->exists("/channels2")) relocateFile(_fs, _fsExtra, "/channels2");
  }
  if (!_fsExtra->exists("/prefs.json")) {
    if (_fs->exists("/prefs.json")) relocateFile(_fs, _fsExtra, "/prefs.json");
    if (_fs->exists("/new_prefs")) relocateFile(_fs, _fsExtra, "/new_prefs");
    if (_fs->exists("/node_prefs")) relocateFile(_fs, _fsExtra, "/node_prefs");
  }
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  File file = openRead(_getStorageFS(), "/adv_blobs");
  uint8_t len = 0;  // 0 = not found
  if (file) {
    BlobRec tmp;
    while (file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      if (memcmp(key, tmp.key, sizeof(tmp.key)) == 0) {  // only match by 7 byte prefix
        len = tmp.len;
        memcpy(dest_buf, tmp.data, len);
        break;
      }
    }
    file.close();
  }
  return len;
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  if (len < PUB_KEY_SIZE+4+SIGNATURE_SIZE || len > MAX_ADVERT_PKT_LEN) return false;
  checkAdvBlobFile();
  File file = _getStorageFS()->open("/adv_blobs", FILE_O_WRITE);
  if (file) {
    uint32_t pos = 0, found_pos = 0;
    uint32_t min_timestamp = 0xFFFFFFFF;

    // search for matching key OR evict by oldest timestamp
    BlobRec tmp;
    file.seek(0);
    while (file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      if (memcmp(key, tmp.key, sizeof(tmp.key)) == 0) {  // only match by 7 byte prefix
        found_pos = pos;
        break;
      }
      if (tmp.timestamp < min_timestamp) {
        min_timestamp = tmp.timestamp;
        found_pos = pos;
      }

      pos += sizeof(tmp);
    }

    memcpy(tmp.key, key, sizeof(tmp.key));  // just record 7 byte prefix of key
    memcpy(tmp.data, src_buf, len);
    tmp.len = len;
    tmp.timestamp = _clock->getCurrentTime();

    file.seek(found_pos);
    file.write((uint8_t *) &tmp, sizeof(tmp));

    file.close();
    return true;
  }
  return false; // error
}
bool DataStore::deleteBlobByKey(const uint8_t key[], int key_len) {
  return true; // this is just a stub on NRF52/STM32 platforms
}
#else
inline void makeBlobPath(const uint8_t key[], int key_len, char* path, size_t path_size) {
  char fname[18];
  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  if (_fs->exists(path)) {
    File f = openRead(_fs, path);
    if (f) {
      int len = f.read(dest_buf, 255); // currently MAX 255 byte blob len supported!!
      f.close();
      return len;
    }
  }
  return 0; // not found
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  File f = openWrite(_fs, path);
  if (f) {
    int n = f.write(src_buf, len);
    f.close();
    if (n == len) return true; // success!

    _fs->remove(path); // blob was only partially written!
  }
  return false; // error
}

bool DataStore::deleteBlobByKey(const uint8_t key[], int key_len) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  _fs->remove(path);
  
  return true; // return true even if file did not exist
}
#endif
