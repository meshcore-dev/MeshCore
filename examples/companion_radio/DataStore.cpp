#include <Arduino.h>
#include "DataStore.h"

#if defined(EXTRAFS) || defined(QSPIFLASH)
  #define MAX_BLOBRECS 100
#else
  #define MAX_BLOBRECS 20
#endif

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
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}
#endif

#define MAX_PATH_LEN 64
static char _tmpPath[MAX_PATH_LEN + sizeof(".tmp")];

static File openWrite(FILESYSTEM* fs, const char* filename) {
  snprintf(_tmpPath, sizeof(_tmpPath), "%s.tmp", filename);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(_tmpPath);
  return fs->open(_tmpPath, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(_tmpPath, "w");
#else
  return fs->open(_tmpPath, "w", true);
#endif
}

static bool commitWrite(FILESYSTEM* fs, const char* filename, bool ok) {
  snprintf(_tmpPath, sizeof(_tmpPath), "%s.tmp", filename);
  if (ok) {
    return fs->rename(_tmpPath, filename);
  }
  fs->remove(_tmpPath);
  return false;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  static uint32_t _ContactsChannelsTotalBlocks = 0;
#endif

void DataStore::begin() {
#if defined(RP2040_PLATFORM)
  identity_store.begin();
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _ContactsChannelsTotalBlocks = _getContactsChannelsFS()->_getFS()->cfg->block_count;
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
    #include <CustomLFS_QSPIFlash.h>
  #elif defined(EXTRAFS)
    #include <CustomLFS.h>
  #else 
    #include <InternalFileSystem.h>
  #endif
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
int _countLfsBlock(void *p, lfs_block_t block){
      if (block > _ContactsChannelsTotalBlocks) {
        MESH_DEBUG_PRINTLN("ERROR: Block %d exceeds filesystem bounds - CORRUPTION DETECTED!", block);
        return LFS_ERR_CORRUPT;  // return error to abort lfs_traverse() gracefully
    }
  lfs_size_t *size = (lfs_size_t*) p;
  *size += 1;
    return 0;
}

lfs_ssize_t _getLfsUsedBlockCount(FILESYSTEM* fs) {
  lfs_size_t size = 0;
  int err = lfs_traverse(fs->_getFS(), _countLfsBlock, &size);
  if (err) {
    MESH_DEBUG_PRINTLN("ERROR: lfs_traverse() error: %d", err);
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
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int usedBlockCount = _getLfsUsedBlockCount(_getContactsChannelsFS());
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
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
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

void DataStore::loadPrefs(NodePrefs& prefs, double& node_lat, double& node_lon) {
  if (_fs->exists("/new_prefs")) {
    loadPrefsInt("/new_prefs", prefs, node_lat, node_lon); // new filename
  } else if (_fs->exists("/node_prefs")) {
    loadPrefsInt("/node_prefs", prefs, node_lat, node_lon);
    savePrefs(prefs, node_lat, node_lon);                // save to new filename
    _fs->remove("/node_prefs"); // remove old
  }
}

void DataStore::loadPrefsInt(const char *filename, NodePrefs& _prefs, double& node_lat, double& node_lon) {
  File file = openRead(_fs, filename);
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs.airtime_factor, sizeof(float));                           // 0
    file.read((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));                      // 4
    file.read(pad, 4);                                                                     // 36
    file.read((uint8_t *)&node_lat, sizeof(node_lat));                                     // 40
    file.read((uint8_t *)&node_lon, sizeof(node_lon));                                     // 48
    file.read((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));                               // 56
    file.read((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));                                   // 60
    file.read((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));                                   // 61
    file.read(pad, 1);                                                                     // 62
    file.read((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)); // 63
    file.read((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));                                   // 64
    file.read((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));               // 68
    file.read((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)); // 69
    file.read((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));   // 70
    file.read((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));   // 71
    file.read((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));             // 72
    file.read((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));     // 76
    file.read((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));                   // 77
    file.read(pad, 2);                                                                     // 78
    file.read((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));                         // 80
    file.read((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));               // 84
    file.read((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));                 // 85
    file.read((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));               // 86
    file.read((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));           // 87

    file.close();
  }
}

void DataStore::savePrefs(const NodePrefs& _prefs, double node_lat, double node_lon) {
  File file = openWrite(_fs, "/new_prefs");
  if (file) {
    uint8_t pad[8];
    memset(pad, 0, sizeof(pad));

    bool ok = (file.write((uint8_t *)&_prefs.airtime_factor, sizeof(float)) == sizeof(float));                             // 0
    ok = ok && (file.write((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name)) == sizeof(_prefs.node_name));             // 4
    ok = ok && (file.write(pad, 4) == 4);                                                                                   // 36
    ok = ok && (file.write((uint8_t *)&node_lat, sizeof(node_lat)) == sizeof(node_lat));                                    // 40
    ok = ok && (file.write((uint8_t *)&node_lon, sizeof(node_lon)) == sizeof(node_lon));                                    // 48
    ok = ok && (file.write((uint8_t *)&_prefs.freq, sizeof(_prefs.freq)) == sizeof(_prefs.freq));                           // 56
    ok = ok && (file.write((uint8_t *)&_prefs.sf, sizeof(_prefs.sf)) == sizeof(_prefs.sf));                                 // 60
    ok = ok && (file.write((uint8_t *)&_prefs.cr, sizeof(_prefs.cr)) == sizeof(_prefs.cr));                                 // 61
    ok = ok && (file.write(pad, 1) == 1);                                                                                   // 62
    ok = ok && (file.write((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)) == sizeof(_prefs.manual_add_contacts)); // 63
    ok = ok && (file.write((uint8_t *)&_prefs.bw, sizeof(_prefs.bw)) == sizeof(_prefs.bw));                                 // 64
    ok = ok && (file.write((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm)) == sizeof(_prefs.tx_power_dbm));   // 68
    ok = ok && (file.write((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)) == sizeof(_prefs.telemetry_mode_base)); // 69
    ok = ok && (file.write((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc)) == sizeof(_prefs.telemetry_mode_loc));    // 70
    ok = ok && (file.write((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env)) == sizeof(_prefs.telemetry_mode_env));    // 71
    ok = ok && (file.write((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base)) == sizeof(_prefs.rx_delay_base));                   // 72
    ok = ok && (file.write((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy)) == sizeof(_prefs.advert_loc_policy));       // 76
    ok = ok && (file.write((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks)) == sizeof(_prefs.multi_acks));                             // 77
    ok = ok && (file.write(pad, 2) == 2);                                                                                   // 78
    ok = ok && (file.write((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin)) == sizeof(_prefs.ble_pin));                  // 80
    ok = ok && (file.write((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet)) == sizeof(_prefs.buzzer_quiet));   // 84
    ok = ok && (file.write((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled)) == sizeof(_prefs.gps_enabled));      // 85
    ok = ok && (file.write((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval)) == sizeof(_prefs.gps_interval));   // 86
    ok = ok && (file.write((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config)) == sizeof(_prefs.autoadd_config)); // 87

    file.close();
    commitWrite(_fs, "/new_prefs", ok);
  }
}

void DataStore::loadContacts(DataStoreHost* host) {
File file = openRead(_getContactsChannelsFS(), "/contacts3");
    if (file) {
      bool full = false;
      while (!full) {
        ContactInfo c;
        uint8_t pub_key[32];
        uint8_t unused;

        bool success = (file.read(pub_key, 32) == 32);
        success = success && (file.read((uint8_t *)&c.name, 32) == 32);
        success = success && (file.read(&c.type, 1) == 1);
        success = success && (file.read(&c.flags, 1) == 1);
        success = success && (file.read(&unused, 1) == 1);
        success = success && (file.read((uint8_t *)&c.sync_since, 4) == 4); // was 'reserved'
        success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
        success = success && (file.read((uint8_t *)&c.last_advert_timestamp, 4) == 4);
        success = success && (file.read(c.out_path, 64) == 64);
        success = success && (file.read((uint8_t *)&c.lastmod, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lat, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lon, 4) == 4);

        if (!success) break; // EOF

        c.id = mesh::Identity(pub_key);
        if (!host->onContactLoaded(c)) full = true;
      }
      file.close();
    }
}

void DataStore::saveContacts(DataStoreHost* host) {
  File file = openWrite(_getContactsChannelsFS(), "/contacts3");
  if (file) {
    uint32_t idx = 0;
    ContactInfo c;
    uint8_t unused = 0;
    bool ok = true;

    while (ok && host->getContactForSave(idx, c)) {
      ok = ok && (file.write(c.id.pub_key, 32) == 32);
      ok = ok && (file.write((uint8_t *)&c.name, 32) == 32);
      ok = ok && (file.write(&c.type, 1) == 1);
      ok = ok && (file.write(&c.flags, 1) == 1);
      ok = ok && (file.write(&unused, 1) == 1);
      ok = ok && (file.write((uint8_t *)&c.sync_since, 4) == 4);
      ok = ok && (file.write((uint8_t *)&c.out_path_len, 1) == 1);
      ok = ok && (file.write((uint8_t *)&c.last_advert_timestamp, 4) == 4);
      ok = ok && (file.write(c.out_path, 64) == 64);
      ok = ok && (file.write((uint8_t *)&c.lastmod, 4) == 4);
      ok = ok && (file.write((uint8_t *)&c.gps_lat, 4) == 4);
      ok = ok && (file.write((uint8_t *)&c.gps_lon, 4) == 4);
      idx++;
    }
    file.close();
    commitWrite(_getContactsChannelsFS(), "/contacts3", ok);
  }
}

void DataStore::loadChannels(DataStoreHost* host) {
    File file = openRead(_getContactsChannelsFS(), "/channels2");
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
  File file = openWrite(_getContactsChannelsFS(), "/channels2");
  if (file) {
    uint8_t channel_idx = 0;
    ChannelDetails ch;
    uint8_t unused[4];
    memset(unused, 0, 4);
    bool ok = true;

    while (ok && host->getChannelForSave(channel_idx, ch)) {
      ok = ok && (file.write(unused, 4) == 4);
      ok = ok && (file.write((uint8_t *)ch.name, 32) == 32);
      ok = ok && (file.write((uint8_t *)ch.channel.secret, 32) == 32);
      channel_idx++;
    }
    file.close();
    commitWrite(_getContactsChannelsFS(), "/channels2", ok);
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
  if (!_getContactsChannelsFS()->exists("/adv_blobs")) {
    File file = openWrite(_getContactsChannelsFS(), "/adv_blobs");
    if (file) {
      BlobRec zeroes;
      memset(&zeroes, 0, sizeof(zeroes));
      bool ok = true;
      for (int i = 0; ok && i < MAX_BLOBRECS; i++) {
        ok = (file.write((uint8_t *) &zeroes, sizeof(zeroes)) == sizeof(zeroes));
      }
      file.close();
      commitWrite(_getContactsChannelsFS(), "/adv_blobs", ok);
    }
  }
}

void DataStore::migrateToSecondaryFS() {
  // migrate old adv_blobs, contacts3 and channels2 files to secondary FS if they don't already exist
  if (!_fsExtra->exists("/adv_blobs") && _fs->exists("/adv_blobs")) {
    File oldFile = openRead(_fs, "/adv_blobs");
    File newFile = openWrite(_fsExtra, "/adv_blobs");
    bool ok = (oldFile && newFile);
    if (ok) {
      BlobRec rec;
      for (int i = 0; ok && i < 20; i++) {
        if (oldFile.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
          ok = (newFile.write((uint8_t *)&rec, sizeof(rec)) == sizeof(rec));
        }
      }
    }
    if (oldFile) oldFile.close();
    if (newFile) newFile.close();
    if (commitWrite(_fsExtra, "/adv_blobs", ok)) _fs->remove("/adv_blobs");
  }
  if (!_fsExtra->exists("/contacts3") && _fs->exists("/contacts3")) {
    File oldFile = openRead(_fs, "/contacts3");
    File newFile = openWrite(_fsExtra, "/contacts3");
    bool ok = (oldFile && newFile);
    if (ok) {
      uint8_t buf[64];
      int n;
      while (ok && (n = oldFile.read(buf, sizeof(buf))) > 0) {
        ok = (newFile.write(buf, n) == n);
      }
    }
    if (oldFile) oldFile.close();
    if (newFile) newFile.close();
    if (commitWrite(_fsExtra, "/contacts3", ok)) _fs->remove("/contacts3");
  }
  if (!_fsExtra->exists("/channels2") && _fs->exists("/channels2")) {
    File oldFile = openRead(_fs, "/channels2");
    File newFile = openWrite(_fsExtra, "/channels2");
    bool ok = (oldFile && newFile);
    if (ok) {
      uint8_t buf[64];
      int n;
      while (ok && (n = oldFile.read(buf, sizeof(buf))) > 0) {
        ok = (newFile.write(buf, n) == n);
      }
    }
    if (oldFile) oldFile.close();
    if (newFile) newFile.close();
    if (commitWrite(_fsExtra, "/channels2", ok)) _fs->remove("/channels2");
  }
  // cleanup nodes which have been testing the extra fs, copy _main.id and new_prefs back to primary
  if (_fsExtra->exists("/_main.id")) {
    File oldFile = openRead(_fsExtra, "/_main.id");
    File newFile = openWrite(_fs, "/_main.id");
    bool ok = (oldFile && newFile);
    if (ok) {
      uint8_t buf[64];
      int n;
      while (ok && (n = oldFile.read(buf, sizeof(buf))) > 0) {
        ok = (newFile.write(buf, n) == n);
      }
    }
    if (oldFile) oldFile.close();
    if (newFile) newFile.close();
    if (commitWrite(_fs, "/_main.id", ok)) _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    File oldFile = openRead(_fsExtra, "/new_prefs");
    File newFile = openWrite(_fs, "/new_prefs");
    bool ok = (oldFile && newFile);
    if (ok) {
      uint8_t buf[64];
      int n;
      while (ok && (n = oldFile.read(buf, sizeof(buf))) > 0) {
        ok = (newFile.write(buf, n) == n);
      }
    }
    if (oldFile) oldFile.close();
    if (newFile) newFile.close();
    if (commitWrite(_fs, "/new_prefs", ok)) _fsExtra->remove("/new_prefs");
  }
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  File file = openRead(_getContactsChannelsFS(), "/adv_blobs");
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
  File file = _getContactsChannelsFS()->open("/adv_blobs", FILE_O_WRITE);
  if (file) {
    uint32_t pos = 0, found_pos = 0;
    uint32_t min_timestamp = 0xFFFFFFFF;

    // search for matching key OR evict by oldest timestmap
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
#else
uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  char path[MAX_PATH_LEN];
  char fname[18];

  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);

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
  char path[MAX_PATH_LEN];
  char fname[18];

  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);

  File f = openWrite(_fs, path);
  if (f) {
    bool ok = (f.write(src_buf, len) == len);
    f.close();
    return commitWrite(_fs, path, ok);
  }
  return false;
}
#endif
