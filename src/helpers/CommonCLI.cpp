#include <Arduino.h>
#include "CommonCLI.h"
#include "ConfigSerializer.h"
#include "FsLastErr.h"
#include "TxtDataHelpers.h"
#include "AdvertDataHelpers.h"
#include "TxtDataHelpers.h"
#include <RTClib.h>
#include <stdarg.h>
#if defined(NRF52_PLATFORM)
  #include "flash/flash_nrf5x.h"
#elif defined(STM32_PLATFORM)
  #include "InternalFileSystem.h"
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

static void repairFeedWatchdog() { }


#ifndef BRIDGE_MAX_BAUD
#define BRIDGE_MAX_BAUD 115200
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

static bool isValidName(const char *n) {
  while (*n) {
    if (*n == '[' || *n == ']' || *n == '\\' || *n == ':' || *n == ',' || *n == '?' || *n == '*') return false;
    n++;
  }
  return true;
}

void CommonCLI::loadPrefs(FILESYSTEM* fs) {
  if (fs->exists("/prefs.json")) {
#if defined(RP2040_PLATFORM)
    File file = fs->open("/prefs.json", "r");
#else
    File file = fs->open("/prefs.json");
#endif
    if (file) {
      _prefs->loadSerial(file);   // new Serial prefs
      file.close();
    }
  } else if (fs->exists("/com_prefs")) {
    loadPrefsInt(fs, "/com_prefs");
    if (savePrefs(fs)) {  // save to new Serial prefs
  //    fs->remove("/com_prefs");  // remove old
    }
  }
}

void CommonCLI::loadPrefsInt(FILESYSTEM* fs, const char* filename) {  // Legacy prefs loader
#if defined(RP2040_PLATFORM)
  File file = fs->open(filename, "r");
#else
  File file = fs->open(filename);
#endif
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs->airtime_factor, sizeof(_prefs->airtime_factor));    // 0
    file.read((uint8_t *)&_prefs->node_name, sizeof(_prefs->node_name));              // 4
    file.read(pad, 4);                                                                // 36
    file.read((uint8_t *)&_prefs->node_lat, sizeof(_prefs->node_lat));                // 40
    file.read((uint8_t *)&_prefs->node_lon, sizeof(_prefs->node_lon));                // 48
    file.read((uint8_t *)&_prefs->password[0], sizeof(_prefs->password));             // 56
    file.read((uint8_t *)&_prefs->freq, sizeof(_prefs->freq));                        // 72
    file.read((uint8_t *)&_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));        // 76
    file.read((uint8_t *)&_prefs->disable_fwd, sizeof(_prefs->disable_fwd));          // 77
    file.read((uint8_t *)&_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.read(pad, 1);                                                                // 79 : 1 byte unused (was rx_boosted_gain in v1.14.1, moved to end for upgrade compat)
    file.read((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.read((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.read((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.read((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.read(pad, 4); // 108 : 4 bytes unused
    file.read((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.read((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.read((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.read((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.read((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.read((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.read((uint8_t *)&_prefs->path_hash_mode, sizeof(_prefs->path_hash_mode));                 // 121
    file.read((uint8_t *)&_prefs->loop_detect, sizeof(_prefs->loop_detect));                       // 122
    file.read(pad, 1);                                                                             // 123
    file.read((uint8_t *)&_prefs->flood_max, sizeof(_prefs->flood_max));                           // 124
    file.read((uint8_t *)&_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));   // 125
    file.read((uint8_t *)&_prefs->interference_threshold, sizeof(_prefs->interference_threshold)); // 126
    file.read((uint8_t *)&_prefs->bridge_enabled, sizeof(_prefs->bridge_enabled));                 // 127
    file.read((uint8_t *)&_prefs->bridge_delay, sizeof(_prefs->bridge_delay));                     // 128
    file.read((uint8_t *)&_prefs->bridge_pkt_src, sizeof(_prefs->bridge_pkt_src));                 // 130
    file.read((uint8_t *)&_prefs->bridge_baud, sizeof(_prefs->bridge_baud));                       // 131
    file.read((uint8_t *)&_prefs->bridge_channel, sizeof(_prefs->bridge_channel));                 // 135
    file.read((uint8_t *)&_prefs->bridge_secret, sizeof(_prefs->bridge_secret));                   // 136
    file.read((uint8_t *)&_prefs->powersaving_enabled, sizeof(_prefs->powersaving_enabled));       // 152
    file.read(pad, 3);                                                                             // 153
    file.read((uint8_t *)&_prefs->gps_enabled, sizeof(_prefs->gps_enabled));                       // 156
    file.read((uint8_t *)&_prefs->gps_interval, sizeof(_prefs->gps_interval));                     // 157
    file.read((uint8_t *)&_prefs->advert_loc_policy, sizeof (_prefs->advert_loc_policy));          // 161
    file.read((uint8_t *)&_prefs->discovery_mod_timestamp, sizeof(_prefs->discovery_mod_timestamp)); // 162
    file.read((uint8_t *)&_prefs->adc_multiplier, sizeof(_prefs->adc_multiplier));                 // 166
    file.read((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));                          // 170
    file.read((uint8_t *)&_prefs->rx_boosted_gain, sizeof(_prefs->rx_boosted_gain));               // 290
    file.read((uint8_t *)&_prefs->flood_max_unscoped, sizeof(_prefs->flood_max_unscoped));         // 291
    file.read((uint8_t *)&_prefs->flood_max_advert, sizeof(_prefs->flood_max_advert));             // 292
    file.read((uint8_t *)&_prefs->radio_fem_rxgain, sizeof(_prefs->radio_fem_rxgain));             // 293
    file.read((uint8_t *)&_prefs->cad_enabled, sizeof(_prefs->cad_enabled));                       // 294
    // next: 295

    // sanitise bad pref values
    _prefs->rx_delay_base = constrain(_prefs->rx_delay_base, 0, 20.0f);
    _prefs->tx_delay_factor = constrain(_prefs->tx_delay_factor, 0, 2.0f);
    _prefs->direct_tx_delay_factor = constrain(_prefs->direct_tx_delay_factor, 0, 2.0f);
    _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 9.0f);
    _prefs->freq = constrain(_prefs->freq, 150.0f, 2500.0f);
    _prefs->bw = constrain(_prefs->bw, 7.8f, 500.0f);
    _prefs->sf = constrain(_prefs->sf, 5, 12);
    _prefs->cr = constrain(_prefs->cr, 5, 8);
    _prefs->tx_power_dbm = constrain(_prefs->tx_power_dbm, -9, 30);
    _prefs->multi_acks = constrain(_prefs->multi_acks, 0, 1);
    _prefs->adc_multiplier = constrain(_prefs->adc_multiplier, 0.0f, 10.0f);
    _prefs->path_hash_mode = constrain(_prefs->path_hash_mode, 0, 2);   // NOTE: mode 3 reserved for future

    // sanitise bad bridge pref values
    _prefs->bridge_enabled = constrain(_prefs->bridge_enabled, 0, 1);
    _prefs->bridge_delay = constrain(_prefs->bridge_delay, 0, 10000);
    _prefs->bridge_pkt_src = constrain(_prefs->bridge_pkt_src, 0, 1);
    _prefs->bridge_baud = constrain(_prefs->bridge_baud, 9600, BRIDGE_MAX_BAUD);
    _prefs->bridge_channel = constrain(_prefs->bridge_channel, 0, 14);

    _prefs->powersaving_enabled = constrain(_prefs->powersaving_enabled, 0, 1);

    _prefs->gps_enabled = constrain(_prefs->gps_enabled, 0, 1);
    _prefs->advert_loc_policy = constrain(_prefs->advert_loc_policy, 0, 2);

    // sanitise settings
    _prefs->rx_boosted_gain = constrain(_prefs->rx_boosted_gain, 0, 1); // boolean
    _prefs->radio_fem_rxgain = constrain(_prefs->radio_fem_rxgain, 0, 1); // boolean
    _prefs->radio_fem_txgain = constrain(_prefs->radio_fem_txgain, 0, 1); // boolean
    _prefs->cad_enabled = constrain(_prefs->cad_enabled, 0, 1); // boolean

    file.close();
  }
}

static char s_last_prefs_save_stage[12];

bool CommonCLI::savePrefs(FILESYSTEM* fs) {
  s_last_prefs_save_stage[0] = 0;
  return saveConfigJsonAtomic(fs, *_prefs, "/prefs.json", "/.prefs.json.new",
                              s_last_prefs_save_stage, sizeof(s_last_prefs_save_stage));
}

#define MIN_LOCAL_ADVERT_INTERVAL   60

void CommonCLI::formatPrefsSaveErr(char* reply) {
  const char* stage = s_last_prefs_save_stage[0] ? s_last_prefs_save_stage : "write";
  FILESYSTEM* fs = _callbacks->getFileSystem();
  fsLastErrReplyForFs(reply, 160, fsLastErrGet(), stage, fs);
}

static bool fsHasIdentity(FILESYSTEM* fs) {
#if defined(ESP32) || defined(RP2040_PLATFORM)
  return fs->exists("/identity/_main.id");
#else
  return fs->exists("/_main.id");
#endif
}

bool CommonCLI::savePrefs() {
  if (_prefs->advert_interval * 2 < MIN_LOCAL_ADVERT_INTERVAL) {
    _prefs->advert_interval = 0;  // turn it off, now that device has been manually configured
  }
  FILESYSTEM* fs = _callbacks->getFileSystem();
  if (fs) {
    return savePrefs(fs);
  }
  _callbacks->savePrefs();
  return true;
}

bool CommonCLI::persistPrefs(char* reply, const char* ok_msg) {
  if (savePrefs()) {
    strcpy(reply, ok_msg);
    return true;
  }
  formatPrefsSaveErr(reply);
  return false;
}

bool CommonCLI::tryPrefsWrite(FILESYSTEM* fs, char* err_stage, size_t err_stage_len) {
  static const char* path = "/.doctor_prefs.json";
  static const char* tmp = "/.doctor_prefs.json.new";
  repairFeedWatchdog();
  fs->remove(path);
  bool success = saveConfigJsonAtomic(fs, *_prefs, path, tmp, err_stage, err_stage_len);
  fs->remove(path);
  fs->remove(tmp);
  repairFeedWatchdog();
  return success;
}

bool CommonCLI::checkFileSystem(char* reply) {
  FILESYSTEM* fs = _callbacks->getFileSystem();
  if (!fs) {
    strcpy(reply, "ERR unsupported");
    return false;
  }

  bool prefs = fs->exists("/prefs.json");
  bool id = fsHasIdentity(fs);
  bool acl = fs->exists("/s_contacts");
  bool regions = fs->exists("/regions2");

  char stage[12];
  stage[0] = 0;
  bool prefs_write_ok = tryPrefsWrite(fs, stage, sizeof(stage));
  if (prefs_write_ok) {
    sprintf(reply, "OK prefs_writeable prefs=%d id=%d acl=%d regions=%d", prefs, id, acl, regions);
  } else if (strcmp(stage, "nospc") == 0) {
    fsLastErrReplyForFs(reply, 160, fsLastErrGet(), stage, fs);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  } else if (fsIsCriticallyFull(fs)) {
    fsLastErrReplyForFs(reply, 160, fsLastErrGet(), stage, fs);
#endif
  } else {
    sprintf(reply, "ERR prefs %s failed prefs=%d id=%d acl=%d regions=%d (try: doctor gc)",
            stage[0] ? stage : "write", prefs, id, acl, regions);
  }
  return prefs_write_ok;
}

bool CommonCLI::wipeFileSystem(char* reply) {
  repairFeedWatchdog();
  if (!_callbacks->formatFileSystem()) {
    strcpy(reply, "ERR format failed");
    return false;
  }
  repairFeedWatchdog();
  if (!_callbacks->remountFileSystem()) {
    strcpy(reply, "ERR remount failed");
    return false;
  }
  strcpy(reply, "OK wiped (reboot required)");
  return true;
}

#if defined(NRF52_PLATFORM)
static bool doctorFsFlashRegion(uint32_t* addr, uint32_t* size) {
#ifdef NRF52840_XXAA
  *addr = 0xED000;
#else
  *addr = 0x6D000;
#endif
  *size = 7u * FLASH_NRF52_PAGE_SIZE;
  return true;
}
#elif defined(STM32_PLATFORM)
static bool doctorFsFlashRegion(uint32_t* addr, uint32_t* size) {
  *addr = LFS_FLASH_ADDR_BASE;
  *size = LFS_FLASH_TOTAL_SIZE;
  return true;
}
#else
static bool doctorFsFlashRegion(uint32_t* addr, uint32_t* size) {
  (void) addr;
  (void) size;
  return false;
}
#endif

static void doctorFsLine(const char* fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n <= 0) return;
  if (n >= (int) sizeof(buf)) n = sizeof(buf) - 1;
  Serial.write((const uint8_t*) buf, n);
  Serial.print("\r\n");
  Serial.flush();
}

static void doctorFsPrintHexLine(uint32_t addr, const uint8_t* data, size_t len) {
  char buf[80];
  int pos = snprintf(buf, sizeof(buf), "FS_DUMP %06X:", addr & 0xFFFFFF);
  for (size_t i = 0; i < len && pos > 0 && pos < (int) sizeof(buf) - 4; i++) {
    pos += snprintf(buf + pos, sizeof(buf) - pos, " %02X", data[i]);
  }
  doctorFsLine("%s", buf);
}

bool CommonCLI::dumpFileSystem(char* reply) {
  uint32_t base = 0;
  uint32_t size = 0;
  if (!doctorFsFlashRegion(&base, &size)) {
    strcpy(reply, "ERR dump unsupported on this platform");
    return false;
  }

  uint8_t buf[16];
  doctorFsLine("FS_DUMP begin addr=0x%X size=%u", base, size);
  for (uint32_t off = 0; off < size; off += sizeof(buf)) {
    repairFeedWatchdog();
    uint32_t chunk = size - off;
    if (chunk > sizeof(buf)) chunk = sizeof(buf);
#if defined(NRF52_PLATFORM)
    if (flash_nrf5x_read(buf, base + off, chunk) <= 0) {
      doctorFsLine("FS_DUMP abort read failed");
      sprintf(reply, "ERR read failed at 0x%X", base + off);
      return false;
    }
#else
    memcpy(buf, (void*) (base + off), chunk);
#endif
    doctorFsPrintHexLine(base + off, buf, chunk);
  }
  doctorFsLine("FS_DUMP end");
  sprintf(reply, "OK dumped %u bytes", size);
  return true;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)

static int doctorFsCountBlock(void* p, lfs_block_t block) {
  (void) block;
  lfs_size_t* count = (lfs_size_t*) p;
  (*count)++;
  return 0;
}

static lfs_ssize_t doctorFsUsedBlocks(lfs_t* lfs) {
  lfs_size_t count = 0;
  if (lfs_traverse(lfs, doctorFsCountBlock, &count) != 0) return -1;
  return (lfs_ssize_t) count;
}

static void doctorFsStatPath(lfs_t* lfs, const char* path) {
  struct lfs_info info;
  if (lfs_stat(lfs, path, &info) == 0) {
    doctorFsLine("FS_STAT file %s %u", path, (unsigned) info.size);
  } else {
    doctorFsLine("FS_STAT file %s missing", path);
  }
}

static void doctorFsListLfs(lfs_t* lfs, const char* path, int depth) {
  lfs_dir_t dir;
  if (lfs_dir_open(lfs, &dir, path) < 0) {
    doctorFsLine("FS_LS err open %s", path);
    return;
  }

  struct lfs_info info;
  while (true) {
    int res = lfs_dir_read(lfs, &dir, &info);
    if (res <= 0) break;
    if (info.name[0] == '.' && (info.name[1] == 0 || (info.name[1] == '.' && info.name[2] == 0))) continue;

    char indent[12];
    int spaces = depth * 2;
    if (spaces > (int) sizeof(indent) - 1) spaces = sizeof(indent) - 1;
    memset(indent, ' ', spaces);
    indent[spaces] = 0;

    if (info.type == LFS_TYPE_DIR) {
      doctorFsLine("FS_LS %s[dir] %s/", indent, info.name);
      char sub[48];
      if (strcmp(path, "/") == 0) {
        snprintf(sub, sizeof(sub), "/%s", info.name);
      } else {
        snprintf(sub, sizeof(sub), "%s/%s", path, info.name);
      }
      doctorFsListLfs(lfs, sub, depth + 1);
    } else {
      doctorFsLine("FS_LS %s[file] %s %u", indent, info.name, (unsigned) info.size);
    }
    repairFeedWatchdog();
  }
  lfs_dir_close(lfs, &dir);
}

static File doctorFsOpenWrite(FILESYSTEM* fs, const char* path) {
  return fs->open(path, FILE_O_WRITE);
}

static bool doctorFsProbeRaw(FILESYSTEM* fs, uint16_t size, char* stage, unsigned long* dt_ms) {
  static const char* final = "/.doctor_probe";
  static const char* tmp = "/.doctor_probe.new";
  unsigned long t0 = millis();

  fs->remove(final);
  fs->remove(tmp);

  File file = doctorFsOpenWrite(fs, tmp);
  if (!file) {
    strcpy(stage, "open");
    *dt_ms = millis() - t0;
    return false;
  }

  uint8_t buf[64];
  memset(buf, 0xA5, sizeof(buf));
  uint16_t left = size;
  while (left > 0) {
    uint16_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
    if (file.write(buf, chunk) != chunk) {
      file.close();
      fs->remove(tmp);
      strcpy(stage, "write");
      *dt_ms = millis() - t0;
      return false;
    }
    left -= chunk;
    repairFeedWatchdog();
  }
  file.close();

  if (!fs->rename(tmp, final)) {
    fs->remove(tmp);
    strcpy(stage, "rename");
    *dt_ms = millis() - t0;
    return false;
  }
  fs->remove(final);
  stage[0] = 0;
  *dt_ms = millis() - t0;
  return true;
}

#elif defined(ESP32)

static File doctorFsOpenWrite(FILESYSTEM* fs, const char* path) {
  return fs->open(path, "w", true);
}

static bool doctorFsProbeRaw(FILESYSTEM* fs, uint16_t size, char* stage, unsigned long* dt_ms) {
  static const char* final = "/.doctor_probe";
  static const char* tmp = "/.doctor_probe.new";
  unsigned long t0 = millis();

  fs->remove(final);
  fs->remove(tmp);

  File file = doctorFsOpenWrite(fs, tmp);
  if (!file) {
    strcpy(stage, "open");
    *dt_ms = millis() - t0;
    return false;
  }

  uint8_t buf[64];
  memset(buf, 0xA5, sizeof(buf));
  uint16_t left = size;
  while (left > 0) {
    uint16_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
    if (file.write(buf, chunk) != chunk) {
      file.close();
      fs->remove(tmp);
      strcpy(stage, "write");
      *dt_ms = millis() - t0;
      return false;
    }
    left -= chunk;
  }
  file.close();

  if (!fs->rename(tmp, final)) {
    fs->remove(tmp);
    strcpy(stage, "rename");
    *dt_ms = millis() - t0;
    return false;
  }
  fs->remove(final);
  stage[0] = 0;
  *dt_ms = millis() - t0;
  return true;
}

#elif defined(RP2040_PLATFORM)

static File doctorFsOpenWrite(FILESYSTEM* fs, const char* path) {
  return fs->open(path, "w");
}

static bool doctorFsProbeRaw(FILESYSTEM* fs, uint16_t size, char* stage, unsigned long* dt_ms) {
  static const char* final = "/.doctor_probe";
  static const char* tmp = "/.doctor_probe.new";
  unsigned long t0 = millis();

  fs->remove(final);
  fs->remove(tmp);

  File file = doctorFsOpenWrite(fs, tmp);
  if (!file) {
    strcpy(stage, "open");
    *dt_ms = millis() - t0;
    return false;
  }

  uint8_t buf[64];
  memset(buf, 0xA5, sizeof(buf));
  uint16_t left = size;
  while (left > 0) {
    uint16_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
    if (file.write(buf, chunk) != chunk) {
      file.close();
      fs->remove(tmp);
      strcpy(stage, "write");
      *dt_ms = millis() - t0;
      return false;
    }
    left -= chunk;
  }
  file.close();

  if (!fs->rename(tmp, final)) {
    fs->remove(tmp);
    strcpy(stage, "rename");
    *dt_ms = millis() - t0;
    return false;
  }
  fs->remove(final);
  stage[0] = 0;
  *dt_ms = millis() - t0;
  return true;
}

#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM) || defined(ESP32) || defined(RP2040_PLATFORM)

static void doctorFsListGeneric(FILESYSTEM* fs) {
  File root = fs->open("/");
  if (!root) {
    doctorFsLine("FS_LS err open /");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      doctorFsLine("FS_LS [dir] %s/", file.name());
    } else {
      doctorFsLine("FS_LS [file] %s %d", file.name(), file.size());
    }
    repairFeedWatchdog();
    file = root.openNextFile();
  }
  root.close();
}

bool CommonCLI::statFileSystem(char* reply) {
  FILESYSTEM* fs = _callbacks->getFileSystem();
  if (!fs) {
    strcpy(reply, "ERR unsupported");
    return false;
  }

  doctorFsLine("FS_STAT begin");
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  lfs_t* lfs = fs->_getFS();
  const lfs_config* cfg = lfs->cfg;
  lfs_ssize_t used_blocks = doctorFsUsedBlocks(lfs);
  uint32_t block_count = cfg->block_count;
  uint32_t block_size = cfg->block_size;
  uint32_t total_bytes = block_count * block_size;
  uint32_t used_bytes = used_blocks >= 0 ? (uint32_t) used_blocks * block_size : 0;
  uint32_t free_bytes = used_blocks >= 0 && used_bytes <= total_bytes ? total_bytes - used_bytes : 0;

  doctorFsLine("FS_STAT total %u", total_bytes);
  doctorFsLine("FS_STAT used~ %u free~ %u", used_bytes, free_bytes);
  doctorFsLine("FS_STAT blocks %u used %ld bsize %u", block_count, (long) used_blocks, block_size);
  doctorFsStatPath(lfs, "/prefs.json");
  doctorFsStatPath(lfs, "/_main.id");
  doctorFsStatPath(lfs, "/s_contacts");
  doctorFsStatPath(lfs, "/regions2");
  sprintf(reply, "OK free~=%u/%u blk=%ld/%u", free_bytes, total_bytes, (long) used_blocks, block_count);
#elif defined(ESP32)
  uint32_t total_bytes = SPIFFS.totalBytes();
  uint32_t used_bytes = SPIFFS.usedBytes();
  uint32_t free_bytes = total_bytes - used_bytes;
  doctorFsLine("FS_STAT total %u used %u free %u", total_bytes, used_bytes, free_bytes);
  static const char* paths[] = {"/prefs.json", "/identity/_main.id", "/s_contacts", "/regions2", NULL};
  for (int i = 0; paths[i]; i++) {
    if (fs->exists(paths[i])) {
      File f = fs->open(paths[i]);
      doctorFsLine("FS_STAT file %s %d", paths[i], f ? (int) f.size() : -1);
      if (f) f.close();
    } else {
      doctorFsLine("FS_STAT file %s missing", paths[i]);
    }
  }
  sprintf(reply, "OK free=%u/%u", free_bytes, total_bytes);
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  fs->info(info);
  doctorFsLine("FS_STAT total %u used %u free %u", info.totalBytes, info.usedBytes, info.totalBytes - info.usedBytes);
  sprintf(reply, "OK free=%u/%u", info.totalBytes - info.usedBytes, info.totalBytes);
#else
  strcpy(reply, "OK");
#endif
  doctorFsLine("FS_STAT end");
  return true;
}

bool CommonCLI::listFileSystem(char* reply) {
  FILESYSTEM* fs = _callbacks->getFileSystem();
  if (!fs) {
    strcpy(reply, "ERR unsupported");
    return false;
  }

  doctorFsLine("FS_LS begin /");
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  doctorFsListLfs(fs->_getFS(), "/", 0);
#else
  doctorFsListGeneric(fs);
#endif
  doctorFsLine("FS_LS end");
  strcpy(reply, "OK see serial FS_LS");
  return true;
}

bool CommonCLI::probeFileSystem(char* reply) {
  FILESYSTEM* fs = _callbacks->getFileSystem();
  if (!fs) {
    strcpy(reply, "ERR unsupported");
    return false;
  }

  static const uint16_t sizes[] = {
    1, 2, 4, 8, 10, 16, 32, 64, 100, 128, 256, 512, 768, 1000, 1280, 1536,
    1800, 2048, 2304, 2560, 2800, 3072, 3584, 4096
  };

  doctorFsLine("FS_PROBE begin raw");
  uint16_t max_ok = 0;
  uint16_t first_fail = 0;
  char fail_stage[12];
  fail_stage[0] = 0;

  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    char stage[12];
    unsigned long dt = 0;
    bool ok = doctorFsProbeRaw(fs, sizes[i], stage, &dt);
    if (ok) {
      doctorFsLine("FS_PROBE raw %u ok %lu", sizes[i], dt);
      max_ok = sizes[i];
    } else {
      doctorFsLine("FS_PROBE raw %u fail %s %lu", sizes[i], stage, dt);
      if (first_fail == 0) {
        first_fail = sizes[i];
        strncpy(fail_stage, stage, sizeof(fail_stage) - 1);
        fail_stage[sizeof(fail_stage) - 1] = 0;
      }
    }
    repairFeedWatchdog();
  }

  char prefs_stage[12];
  prefs_stage[0] = 0;
  unsigned long prefs_dt = millis();
  bool prefs_ok = tryPrefsWrite(fs, prefs_stage, sizeof(prefs_stage));
  prefs_dt = millis() - prefs_dt;
  doctorFsLine("FS_PROBE prefs_json %s %s %lu",
               prefs_ok ? "ok" : "fail", prefs_stage[0] ? prefs_stage : "-", prefs_dt);
  doctorFsLine("FS_PROBE end");

  if (first_fail == 0) {
    sprintf(reply, "OK raw_max=%u prefs=%s", max_ok, prefs_ok ? "ok" : "fail");
  } else {
    sprintf(reply, "OK raw_max=%u fail>=%u@%s prefs=%s",
            max_ok, first_fail, fail_stage[0] ? fail_stage : "?", prefs_ok ? "ok" : "fail");
  }
  return true;
}

#endif

bool CommonCLI::gcFileSystem(char* reply) {
  FILESYSTEM* fs = _callbacks->getFileSystem();
  if (!fs) {
    strcpy(reply, "ERR unsupported");
    return false;
  }

  repairFeedWatchdog();
  uint32_t removed = 0;

  static const char* files[] = {
    "/packet_log",
    "/com_prefs",
    "/.doctor_prefs.json",
    "/.doctor_prefs.json.new",
    "/.doctor_probe",
    "/.doctor_probe.new",
    NULL
  };

  for (int i = 0; files[i]; i++) {
    if (fs->exists(files[i])) {
      fs->remove(files[i]);
      removed++;
      repairFeedWatchdog();
    }
  }

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (fs->exists("/prefs")) {
    fs->rmdir_r("/prefs");
    removed++;
  }
#endif

  sprintf(reply, "OK gc removed %u item(s)", removed);
  return true;
}

void CommonCLI::handleDoctor(uint32_t sender_timestamp, const char* args, char* reply) {
  while (*args == ' ') args++;

  if (*args == 0) {
    strcpy(reply, "usage: doctor check|stat|ls|probe|dump|gc");
  } else if (memcmp(args, "gc", 2) == 0 && (args[2] == 0 || args[2] == ' ')) {
    gcFileSystem(reply);
  } else if (memcmp(args, "check", 5) == 0 && (args[5] == 0 || args[5] == ' ')) {
    checkFileSystem(reply);
  } else if (memcmp(args, "dump", 4) == 0 && (args[4] == 0 || args[4] == ' ')) {
    if (sender_timestamp != 0) {
      strcpy(reply, "ERR dump requires USB");
    } else {
      dumpFileSystem(reply);
    }
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM) || defined(ESP32) || defined(RP2040_PLATFORM)
  } else if (memcmp(args, "stat", 4) == 0 && (args[4] == 0 || args[4] == ' ')) {
    if (sender_timestamp != 0) {
      strcpy(reply, "ERR stat requires USB");
    } else {
      statFileSystem(reply);
    }
  } else if (memcmp(args, "ls", 2) == 0 && (args[2] == 0 || args[2] == ' ')) {
    if (sender_timestamp != 0) {
      strcpy(reply, "ERR ls requires USB");
    } else {
      listFileSystem(reply);
    }
  } else if (memcmp(args, "probe", 5) == 0 && (args[5] == 0 || args[5] == ' ')) {
    if (sender_timestamp != 0) {
      strcpy(reply, "ERR probe requires USB");
    } else {
      probeFileSystem(reply);
    }
#endif
  } else {
    strcpy(reply, "usage: doctor check|stat|ls|probe|dump|gc");
  }
}

uint8_t CommonCLI::buildAdvertData(uint8_t node_type, uint8_t* app_data) {
  if (_prefs->advert_loc_policy == ADVERT_LOC_NONE) {
    AdvertDataBuilder builder(node_type, _prefs->node_name);
    return builder.encodeTo(app_data);
  } else if (_prefs->advert_loc_policy == ADVERT_LOC_SHARE) {
    AdvertDataBuilder builder(node_type, _prefs->node_name, _sensors->node_lat, _sensors->node_lon);
    return builder.encodeTo(app_data);
  } else {
    AdvertDataBuilder builder(node_type, _prefs->node_name, _prefs->node_lat, _prefs->node_lon);
    return builder.encodeTo(app_data);
  }
}

void CommonCLI::handleCommand(uint32_t sender_timestamp, char* command, char* reply) {
    if (memcmp(command, "poweroff", 8) == 0 || memcmp(command, "shutdown", 8) == 0) {
      _board->powerOff();  // doesn't return
    } else if (memcmp(command, "reboot", 6) == 0) {
      _board->reboot();  // doesn't return
    } else if (memcmp(command, "clkreboot", 9) == 0) {
      // Reset clock
      getRTCClock()->setCurrentTime(1715770351);  // 15 May 2024, 8:50pm
      _board->reboot();  // doesn't return
     } else if (memcmp(command, "advert.zerohop", 14) == 0 && (command[14] == 0 || command[14] == ' ')) {
      // send zerohop advert
      _callbacks->sendSelfAdvertisement(1500, false);  // longer delay, give CLI response time to be sent first
      strcpy(reply, "OK - zerohop advert sent");
    } else if (memcmp(command, "advert", 6) == 0) {
      // send flood advert
      _callbacks->sendSelfAdvertisement(1500, true);  // longer delay, give CLI response time to be sent first
      strcpy(reply, "OK - Advert sent");
    } else if (memcmp(command, "clock sync", 10) == 0) {
      uint32_t curr = getRTCClock()->getCurrentTime();
      if (sender_timestamp > curr) {
        getRTCClock()->setCurrentTime(sender_timestamp + 1);
        uint32_t now = getRTCClock()->getCurrentTime();
        DateTime dt = DateTime(now);
        sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
      } else {
        strcpy(reply, "ERR: clock cannot go backwards");
      }
    } else if (memcmp(command, "start ota", 9) == 0) {
      if (!_board->startOTAUpdate(_prefs->node_name, reply)) {
        strcpy(reply, "Error");
      }
    } else if (memcmp(command, "clock", 5) == 0) {
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(reply, "%02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else if (memcmp(command, "time ", 5) == 0) {  // set time (to epoch seconds)
      uint32_t secs = _atoi(&command[5]);
      uint32_t curr = getRTCClock()->getCurrentTime();
      if (secs > curr) {
        getRTCClock()->setCurrentTime(secs);
        uint32_t now = getRTCClock()->getCurrentTime();
        DateTime dt = DateTime(now);
        sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
      } else {
        strcpy(reply, "(ERR: clock cannot go backwards)");
      }
    } else if (memcmp(command, "neighbors", 9) == 0) {
      _callbacks->formatNeighborsReply(reply);
    } else if (memcmp(command, "neighbor.remove ", 16) == 0) {
      const char* hex = &command[16];
      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min((int)strlen(hex), PUB_KEY_SIZE*2);
      int pubkey_len = hex_len / 2;
      if (mesh::Utils::fromHex(pubkey, pubkey_len, hex)) {
        _callbacks->removeNeighbor(pubkey, pubkey_len);
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "ERR: bad pubkey");
      }
    } else if (memcmp(command, "tempradio ", 10) == 0) {
      strcpy(tmp, &command[10]);
      const char *parts[5];
      int num = mesh::Utils::parseTextParts(tmp, parts, 5);
      float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
      float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
      uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
      uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
      int temp_timeout_mins  = num > 4 ? atoi(parts[4]) : 0;
      if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error, invalid params");
      }
    } else if (memcmp(command, "password ", 9) == 0) {
      // change admin password
      StrHelper::strncpy(_prefs->password, &command[9], sizeof(_prefs->password));
      if (!savePrefs()) {
        formatPrefsSaveErr(reply);
      } else {
        sprintf(reply, "password now: ");
        StrHelper::strncpy(&reply[14], _prefs->password, 160-15);   // echo back just to let admin know for sure!!
      }
    } else if (memcmp(command, "clear stats", 11) == 0) {
      _callbacks->clearStats();
      strcpy(reply, "(OK - stats reset)");
    } else if (memcmp(command, "get ", 4) == 0) {
      handleGetCmd(sender_timestamp, command, reply);
    } else if (memcmp(command, "set ", 4) == 0) {
      handleSetCmd(sender_timestamp, command, reply);
    } else if (memcmp(command, "doctor", 6) == 0 && (command[6] == 0 || command[6] == ' ')) {
      handleDoctor(sender_timestamp, &command[6], reply);
    } else if (sender_timestamp == 0 && strcmp(command, "erase") == 0) {
      if (_callbacks->getFileSystem()) {
        wipeFileSystem(reply);
      } else {
        bool s = _callbacks->formatFileSystem();
        sprintf(reply, "File system erase: %s", s ? "OK" : "Err");
      }
    } else if (memcmp(command, "ver", 3) == 0) {
      sprintf(reply, "%s (Build: %s)", _callbacks->getFirmwareVer(), _callbacks->getBuildDate());
    } else if (memcmp(command, "board", 5) == 0) {
      sprintf(reply, "%s", _board->getManufacturerName());
    } else if (memcmp(command, "sensor get ", 11) == 0) {
      const char* key = command + 11;
      const char* val = _sensors->getSettingByKey(key);
      if (val != NULL) {
        sprintf(reply, "> %s", val);
      } else {
        strcpy(reply, "null");
      }
    } else if (memcmp(command, "sensor set ", 11) == 0) {
      strcpy(tmp, &command[11]);
      const char *parts[2];
      int num = mesh::Utils::parseTextParts(tmp, parts, 2, ' ');
      const char *key = (num > 0) ? parts[0] : "";
      const char *value = (num > 1) ? parts[1] : "null";
      if (_sensors->setSettingValue(key, value)) {
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "can't find custom var");
      }
    } else if (memcmp(command, "sensor list", 11) == 0) {
      char* dp = reply;
      int start = 0;
      int end = _sensors->getNumSettings();
      if (strlen(command) > 11) {
        start = _atoi(command+12);
      }
      if (start >= end) {
        strcpy(reply, "no custom var");
      } else {
        sprintf(dp, "%d vars\n", end);
        dp = strchr(dp, 0);
        int i;
        for (i = start; i < end && (dp-reply < 134); i++) {
          sprintf(dp, "%s=%s\n",
            _sensors->getSettingName(i),
            _sensors->getSettingValue(i));
          dp = strchr(dp, 0);
        }
        if (i < end) {
          sprintf(dp, "... next:%d", i);
        } else {
          *(dp-1) = 0; // remove last CR
        }
      }
    } else if (memcmp(command, "region", 6) == 0) {
      handleRegionCmd(command, reply);
#if ENV_INCLUDE_GPS == 1
    } else if (memcmp(command, "gps on", 6) == 0) {
      if (_sensors->setSettingValue("gps", "1")) {
        _prefs->gps_enabled = 1;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "gps toggle not found");
      }
    } else if (memcmp(command, "gps off", 7) == 0) {
      if (_sensors->setSettingValue("gps", "0")) {
        _prefs->gps_enabled = 0;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "gps toggle not found");
      }
    } else if (memcmp(command, "gps sync", 8) == 0) {
      LocationProvider * l = _sensors->getLocationProvider();
      if (l != NULL) {
        l->syncTime();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "gps provider not found");
      }
    } else if (memcmp(command, "gps setloc", 10) == 0) {
      _prefs->node_lat = _sensors->node_lat;
      _prefs->node_lon = _sensors->node_lon;
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "gps advert", 10) == 0) {
      if (strlen(command) == 10) {
        switch (_prefs->advert_loc_policy) {
          case ADVERT_LOC_NONE:
            strcpy(reply, "> none");
            break;
          case ADVERT_LOC_PREFS:
            strcpy(reply, "> prefs");
            break;
          case ADVERT_LOC_SHARE:
            strcpy(reply, "> share");
            break;
          default:
            strcpy(reply, "error");
        }
      } else if (memcmp(command+11, "none", 4) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_NONE;
        savePrefs();
        strcpy(reply, "ok");
      } else if (memcmp(command+11, "share", 5) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_SHARE;
        savePrefs();
        strcpy(reply, "ok");
      } else if (memcmp(command+11, "prefs", 5) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_PREFS;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "error");
      }
    } else if (memcmp(command, "gps", 3) == 0) {
      LocationProvider * l = _sensors->getLocationProvider();
      if (l != NULL) {
        bool enabled = l->isEnabled(); // is EN pin on ?
        bool fix = l->isValid();       // has fix ?
        int sats = l->satellitesCount();
        bool active = !strcmp(_sensors->getSettingByKey("gps"), "1");
        if (enabled) {
          sprintf(reply, "on, %s, %s, %d sats",
            active?"active":"deactivated",
            fix?"fix":"no fix",
            sats);
        } else {
          strcpy(reply, "off");
        }
      } else {
        strcpy(reply, "Can't find GPS");
      }
#endif
    } else if (memcmp(command, "powersaving on", 14) == 0) {
#if defined(NRF52_PLATFORM)
      _prefs->powersaving_enabled = 1;
      savePrefs();
      strcpy(reply, "on - Immediate effect");
#elif defined(ESP32) && !defined(WITH_BRIDGE)
      _prefs->powersaving_enabled = 1;
      savePrefs();
      strcpy(reply, "on - After 2 minutes");
#elif defined(WITH_BRIDGE)
      strcpy(reply, "Bridge not supported");
#else
      strcpy(reply, "Board not supported");
#endif
    } else if (memcmp(command, "powersaving off", 15) == 0) {
      _prefs->powersaving_enabled = 0;
      savePrefs();
      strcpy(reply, "off");
    } else if (memcmp(command, "powersaving", 11) == 0) {
      if (_prefs->powersaving_enabled) {
        strcpy(reply, "on");
      } else {
        strcpy(reply, "off");
      }
    } else if (memcmp(command, "log start", 9) == 0) {
      _callbacks->setLoggingOn(true);
      strcpy(reply, "   logging on");
    } else if (memcmp(command, "log stop", 8) == 0) {
      _callbacks->setLoggingOn(false);
      strcpy(reply, "   logging off");
    } else if (memcmp(command, "log erase", 9) == 0) {
      _callbacks->eraseLogFile();
      strcpy(reply, "   log erased");
    } else if (sender_timestamp == 0 && memcmp(command, "log", 3) == 0) {
      _callbacks->dumpLogFile();
      strcpy(reply, "   EOF");
    } else if (sender_timestamp == 0 && memcmp(command, "stats-packets", 13) == 0 && (command[13] == 0 || command[13] == ' ')) {
      _callbacks->formatPacketStatsReply(reply);
    } else if (sender_timestamp == 0 && memcmp(command, "stats-radio", 11) == 0 && (command[11] == 0 || command[11] == ' ')) {
      _callbacks->formatRadioStatsReply(reply);
    } else if (sender_timestamp == 0 && memcmp(command, "stats-core", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
      _callbacks->formatStatsReply(reply);
    } else {
      strcpy(reply, "Unknown command");
    }
}

void CommonCLI::handleSetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  const char* config = &command[4];
  if (memcmp(config, "dutycycle ", 10) == 0) {
    float dc = atof(&config[10]);
    if (dc < 1 || dc > 100) {
      strcpy(reply, "ERROR: dutycycle must be 1-100");
    } else {
      _prefs->airtime_factor = (100.0f / dc) - 1.0f;
      if (!savePrefs()) {
        formatPrefsSaveErr(reply);
      } else {
        float actual = 100.0f / (_prefs->airtime_factor + 1.0f);
        int a_int = (int)actual;
        int a_frac = (int)((actual - a_int) * 10.0f + 0.5f);
        sprintf(reply, "OK - %d.%d%%", a_int, a_frac);
      }
    }
  } else if (memcmp(config, "af ", 3) == 0) {
    _prefs->airtime_factor = atof(&config[3]);
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "int.thresh ", 11) == 0) {
    _prefs->interference_threshold = atoi(&config[11]);
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "cad ", 4) == 0) {
    _prefs->cad_enabled = memcmp(&config[4], "on", 2) == 0;
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "agc.reset.interval ", 19) == 0) {
    _prefs->agc_reset_interval = atoi(&config[19]) / 4;
    if (!savePrefs()) {
      formatPrefsSaveErr(reply);
    } else {
      sprintf(reply, "OK - interval rounded to %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
    }
  } else if (memcmp(config, "multi.acks ", 11) == 0) {
    _prefs->multi_acks = atoi(&config[11]);
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "allow.read.only ", 16) == 0) {
    _prefs->allow_read_only = memcmp(&config[16], "on", 2) == 0;
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "flood.advert.interval ", 22) == 0) {
    int hours = _atoi(&config[22]);
    if ((hours > 0 && hours < 3) || (hours > 168)) {
      strcpy(reply, "Error: interval range is 3-168 hours");
    } else {
      _prefs->flood_advert_interval = (uint8_t)(hours);
      _callbacks->updateFloodAdvertTimer();
      persistPrefs(reply, "OK");
    }
  } else if (memcmp(config, "advert.interval ", 16) == 0) {
    int mins = _atoi(&config[16]);
    if ((mins > 0 && mins < MIN_LOCAL_ADVERT_INTERVAL) || (mins > 240)) {
      sprintf(reply, "Error: interval range is %d-240 minutes", MIN_LOCAL_ADVERT_INTERVAL);
    } else {
      _prefs->advert_interval = (uint8_t)(mins / 2);
      _callbacks->updateAdvertTimer();
      persistPrefs(reply, "OK");
    }
  } else if (memcmp(config, "guest.password ", 15) == 0) {
    StrHelper::strncpy(_prefs->guest_password, &config[15], sizeof(_prefs->guest_password));
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "prv.key ", 8) == 0) {
    uint8_t prv_key[PRV_KEY_SIZE];
    bool success = mesh::Utils::fromHex(prv_key, PRV_KEY_SIZE, &config[8]);
    // only allow rekey if key is valid
    if (success && mesh::LocalIdentity::validatePrivateKey(prv_key)) {
      mesh::LocalIdentity new_id;
      new_id.readFrom(prv_key, PRV_KEY_SIZE);
      _callbacks->saveIdentity(new_id);
      strcpy(reply, "OK, reboot to apply! New pubkey: ");
      mesh::Utils::toHex(&reply[33], new_id.pub_key, PUB_KEY_SIZE);
    } else {
      strcpy(reply, "Error, bad key");
    }
  } else if (memcmp(config, "name ", 5) == 0) {
    if (isValidName(&config[5])) {
      StrHelper::strncpy(_prefs->node_name, &config[5], sizeof(_prefs->node_name));
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error, bad chars");
    }
  } else if (memcmp(config, "repeat ", 7) == 0) {
    _prefs->disable_fwd = memcmp(&config[7], "off", 3) == 0;
    persistPrefs(reply, _prefs->disable_fwd ? "OK - repeat is now OFF" : "OK - repeat is now ON");
  } else if (memcmp(config, "radio.rxgain ", 13) == 0) {
    bool enabled = memcmp(&config[13], "on", 2) == 0;
    _prefs->rx_boosted_gain = enabled;
    if (!savePrefs()) {
      formatPrefsSaveErr(reply);
    } else if (_callbacks->setRxBoostedGain(enabled)) {
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error: unsupported");
    }
  } else if (memcmp(config, "radio.fem.rxgain ", 17) == 0) {
    if (!_board->canControlLoRaFemLna()) {
      strcpy(reply, "Error: unsupported");
    } else if (memcmp(&config[17], "on", 2) == 0) {
      if (_board->setLoRaFemLnaEnabled(true)) {
        _prefs->radio_fem_rxgain = 1;
        persistPrefs(reply, "OK - LoRa FEM RX gain on");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else if (memcmp(&config[17], "off", 3) == 0) {
      if (_board->setLoRaFemLnaEnabled(false)) {
        _prefs->radio_fem_rxgain = 0;
        persistPrefs(reply, "OK - LoRa FEM RX gain off");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else {
      strcpy(reply, "Error: state must be on or off");
    }
  } else if (memcmp(config, "radio.fem.txgain ", 17) == 0) {
    if (!_board->canControlLoRaFemPaGain()) {
      strcpy(reply, "Error: unsupported");
    } else if (memcmp(&config[17], "on", 2) == 0) {
      if (_board->setLoRaFemPaGainEnabled(true)) {
        _prefs->radio_fem_txgain = 1;
        persistPrefs(reply, "OK - LoRa FEM TX gain on");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM TX gain");
      }
    } else if (memcmp(&config[17], "off", 3) == 0) {
      if (_board->setLoRaFemPaGainEnabled(false)) {
        _prefs->radio_fem_txgain = 0;
        persistPrefs(reply, "OK - LoRa FEM TX gain off");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM TX gain");
      }
    } else {
      strcpy(reply, "Error: state must be on or off");
    }
  } else if (memcmp(config, "radio ", 6) == 0) {
    strcpy(tmp, &config[6]);
    const char *parts[4];
    int num = mesh::Utils::parseTextParts(tmp, parts, 4);
    float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
    float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
    uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
    uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
    if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
      _prefs->sf = sf;
      _prefs->cr = cr;
      _prefs->freq = freq;
      _prefs->bw = bw;
      persistPrefs(reply, "OK - reboot to apply");
    } else {
      strcpy(reply, "Error, invalid radio params");
    }
  } else if (memcmp(config, "lat ", 4) == 0) {
    _prefs->node_lat = atof(&config[4]);
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "lon ", 4) == 0) {
    _prefs->node_lon = atof(&config[4]);
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "rxdelay ", 8) == 0) {
    float db = atof(&config[8]);
    if (db >= 0 && db <= 20.0f) {
      _prefs->rx_delay_base = db;
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-20");
    }
  } else if (memcmp(config, "txdelay ", 8) == 0) {
    float f = atof(&config[8]);
    if (f >= 0 && f <= 2.0f) {
      _prefs->tx_delay_factor = f;
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-2");
    }
  } else if (memcmp(config, "flood.max.unscoped ", 19) == 0) {
    uint8_t m = atoi(&config[19]);
    if (m <= 64) {
      _prefs->flood_max_unscoped = m;
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error, max 64");
    } 
  } else if (memcmp(config, "flood.max.advert ", 17) == 0) {
    uint8_t m = atoi(&config[17]);
    if (m <= 64) {
      _prefs->flood_max_advert = m;
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error, max 64");
    }
  } else if (memcmp(config, "flood.max ", 10) == 0) {
    uint8_t m = atoi(&config[10]);
    if (m <= 64) {
      _prefs->flood_max = m;
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error, max 64");
    }
  } else if (memcmp(config, "direct.txdelay ", 15) == 0) {
    float f = atof(&config[15]);
    if (f >= 0 && f <= 2.0f) {
      _prefs->direct_tx_delay_factor = f;
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-2");
    }
  } else if (memcmp(config, "owner.info ", 11) == 0) {
    config += 11;
    char *dp = _prefs->owner_info;
    while (*config && dp - _prefs->owner_info < sizeof(_prefs->owner_info)-1) {
      *dp++ = (*config == '|') ? '\n' : *config;    // translate '|' to newline chars
      config++;
    }
    *dp = 0;
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "path.hash.mode ", 15) == 0) {
    config += 15;
    uint8_t mode = atoi(config);
    if (mode < 3) {
      _prefs->path_hash_mode = mode;
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0,1, or 2");
    }
  } else if (memcmp(config, "loop.detect ", 12) == 0) {
    config += 12;
    uint8_t mode;
    if (memcmp(config, "off", 3) == 0) {
      mode = LOOP_DETECT_OFF;
    } else if (memcmp(config, "minimal", 7) == 0) {
      mode = LOOP_DETECT_MINIMAL;
    } else if (memcmp(config, "moderate", 8) == 0) {
      mode = LOOP_DETECT_MODERATE;
    } else if (memcmp(config, "strict", 6) == 0) {
      mode = LOOP_DETECT_STRICT;
    } else {
      mode = 0xFF;
      strcpy(reply, "Error, must be: off, minimal, moderate, or strict");
    }
    if (mode != 0xFF) {
      _prefs->loop_detect = mode;
      persistPrefs(reply, "OK");
    }
  } else if (memcmp(config, "tx ", 3) == 0) {
    _prefs->tx_power_dbm = atoi(&config[3]);
    if (savePrefs()) {
      _callbacks->setTxPower(_prefs->tx_power_dbm);
      strcpy(reply, "OK");
    } else {
      formatPrefsSaveErr(reply);
    }
  } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
    _prefs->freq = atof(&config[5]);
    persistPrefs(reply, "OK - reboot to apply");
#ifdef WITH_BRIDGE
  } else if (memcmp(config, "bridge.enabled ", 15) == 0) {
    _prefs->bridge_enabled = memcmp(&config[15], "on", 2) == 0;
    _callbacks->setBridgeState(_prefs->bridge_enabled);
    persistPrefs(reply, "OK");
  } else if (memcmp(config, "bridge.delay ", 13) == 0) {
    int delay = _atoi(&config[13]);
    if (delay >= 0 && delay <= 10000) {
      _prefs->bridge_delay = (uint16_t)delay;
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error: delay must be between 0-10000 ms");
    }
  } else if (memcmp(config, "bridge.source ", 14) == 0) {
    _prefs->bridge_pkt_src = memcmp(&config[14], "rx", 2) == 0;
    persistPrefs(reply, "OK");
#endif
#ifdef WITH_RS232_BRIDGE
  } else if (memcmp(config, "bridge.baud ", 12) == 0) {
    uint32_t baud = atoi(&config[12]);
    if (baud >= 9600 && baud <= BRIDGE_MAX_BAUD) {
      _prefs->bridge_baud = (uint32_t)baud;
      _callbacks->restartBridge();
      persistPrefs(reply, "OK");
    } else {
      sprintf(reply, "Error: baud rate must be between 9600-%d",BRIDGE_MAX_BAUD);
    }
#endif
#ifdef WITH_ESPNOW_BRIDGE
  } else if (memcmp(config, "bridge.channel ", 15) == 0) {
    int ch = atoi(&config[15]);
    if (ch > 0 && ch < 15) {
      _prefs->bridge_channel = (uint8_t)ch;
      _callbacks->restartBridge();
      persistPrefs(reply, "OK");
    } else {
      strcpy(reply, "Error: channel must be between 1-14");
    }
  } else if (memcmp(config, "bridge.secret ", 14) == 0) {
    StrHelper::strncpy(_prefs->bridge_secret, &config[14], sizeof(_prefs->bridge_secret));
    _callbacks->restartBridge();
    persistPrefs(reply, "OK");
#endif
  } else if (memcmp(config, "adc.multiplier ", 15) == 0) {
    _prefs->adc_multiplier = atof(&config[15]);
    if (_board->setAdcMultiplier(_prefs->adc_multiplier)) {
      if (!savePrefs()) {
        formatPrefsSaveErr(reply);
      } else if (_prefs->adc_multiplier == 0.0f) {
        strcpy(reply, "OK - using default board multiplier");
      } else {
        sprintf(reply, "OK - multiplier set to %.3f", _prefs->adc_multiplier);
      }
    } else {
      _prefs->adc_multiplier = 0.0f;
      strcpy(reply, "Error: unsupported");
    };
  #if defined(USE_LR2021)
  } else if (memcmp(config, "extra.sf ", 9) == 0) {
    strcpy(tmp, &config[9]);
    const char *parts[4];
    uint8_t sideDetSFs[4];
    int num = mesh::Utils::parseTextParts(tmp, parts, 4);
    if (num > 3) {
      sprintf(reply, "Invalid extra SF config");
    } else {
      for (int i = 0; i < num; i++) {
        sideDetSFs[i] = atoi(parts[i]);
      }
      sideDetSFs[num] = 0;
      if (_callbacks->configSideDetectors(sideDetSFs, num, _prefs->bw)) {
        for (int i = 0; i <= num; i++) _prefs->extra_sf[i] = sideDetSFs[i];
        if (savePrefs()) {
          sprintf(reply, "OK - extra SFs set");
        } else {
          formatPrefsSaveErr(reply);
        }
      } else {
        sprintf(reply, "Invalid extra SF config");
      }
    }
  #endif
  } else {
    strcpy(reply, "unknown config: ");
    StrHelper::strncpy(&reply[16], config, 160-17);
  }
}

void CommonCLI::handleGetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  const char* config = &command[4];
  if (memcmp(config, "dutycycle", 9) == 0) {
    float dc = 100.0f / (_prefs->airtime_factor + 1.0f);
    int dc_int = (int)dc;
    int dc_frac = (int)((dc - dc_int) * 10.0f + 0.5f);
    sprintf(reply, "> %d.%d%%", dc_int, dc_frac);
  } else if (memcmp(config, "af", 2) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->airtime_factor));
  } else if (memcmp(config, "int.thresh", 10) == 0) {
    sprintf(reply, "> %d", (uint32_t) _prefs->interference_threshold);
  } else if (memcmp(config, "cad", 3) == 0) {
    sprintf(reply, "> %s", _prefs->cad_enabled ? "on" : "off");
  } else if (memcmp(config, "agc.reset.interval", 18) == 0) {
    sprintf(reply, "> %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
  } else if (memcmp(config, "multi.acks", 10) == 0) {
    sprintf(reply, "> %d", (uint32_t) _prefs->multi_acks);
  } else if (memcmp(config, "allow.read.only", 15) == 0) {
    sprintf(reply, "> %s", _prefs->allow_read_only ? "on" : "off");
  } else if (memcmp(config, "flood.advert.interval", 21) == 0) {
    sprintf(reply, "> %d", ((uint32_t) _prefs->flood_advert_interval));
  } else if (memcmp(config, "advert.interval", 15) == 0) {
    sprintf(reply, "> %d", ((uint32_t) _prefs->advert_interval) * 2);
  } else if (memcmp(config, "guest.password", 14) == 0) {
    sprintf(reply, "> %s", _prefs->guest_password);
  } else if (sender_timestamp == 0 && memcmp(config, "prv.key", 7) == 0) {  // from serial command line only
    uint8_t prv_key[PRV_KEY_SIZE];
    int len = _callbacks->getSelfId().writeTo(prv_key, PRV_KEY_SIZE);
    mesh::Utils::toHex(tmp, prv_key, len);
    sprintf(reply, "> %s", tmp);
  } else if (memcmp(config, "name", 4) == 0) {
    sprintf(reply, "> %s", _prefs->node_name);
  } else if (memcmp(config, "repeat", 6) == 0) {
    sprintf(reply, "> %s", _prefs->disable_fwd ? "off" : "on");
  } else if (memcmp(config, "lat", 3) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lat));
  } else if (memcmp(config, "lon", 3) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lon));
  } else if (memcmp(config, "radio.rxgain", 12) == 0) {
    sprintf(reply, "> %s", _prefs->rx_boosted_gain ? "on" : "off");
  } else if (memcmp(config, "radio.fem.rxgain", 16) == 0) {
    if (!_board->canControlLoRaFemLna()) {
      strcpy(reply, "Error: unsupported");
    } else {
      sprintf(reply, "> %s", _board->isLoRaFemLnaEnabled() ? "on" : "off");
    }
  } else if (memcmp(config, "radio.fem.txgain", 16) == 0) {
    if (!_board->canControlLoRaFemPaGain()) {
      strcpy(reply, "Error: unsupported");
    } else {
      sprintf(reply, "> %s", _board->isLoRaFemPaGainEnabled() ? "on" : "off");
    }
  } else if (memcmp(config, "radio", 5) == 0) {
    char freq[16], bw[16];
    strcpy(freq, StrHelper::ftoa(_prefs->freq));
    strcpy(bw, StrHelper::ftoa3(_prefs->bw));
    sprintf(reply, "> %s,%s,%d,%d", freq, bw, (uint32_t)_prefs->sf, (uint32_t)_prefs->cr);
  } else if (memcmp(config, "rxdelay", 7) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->rx_delay_base));
  } else if (memcmp(config, "txdelay", 7) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->tx_delay_factor));
  } else if (memcmp(config, "flood.max.advert", 16) == 0) {
    sprintf(reply, "> %d", (uint32_t)_prefs->flood_max_advert);
  } else if (memcmp(config, "flood.max.unscoped", 18) == 0) {
    sprintf(reply, "> %d", (uint32_t)_prefs->flood_max_unscoped);
  } else if (memcmp(config, "flood.max", 9) == 0) {
    sprintf(reply, "> %d", (uint32_t)_prefs->flood_max);
  } else if (memcmp(config, "direct.txdelay", 14) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->direct_tx_delay_factor));
  } else if (memcmp(config, "owner.info", 10) == 0) {
    auto start = reply;
    *reply++ = '>';
    *reply++ = ' ';
    const char* sp = _prefs->owner_info;
    while (*sp && reply - start < 159) {
      *reply++ = (*sp == '\n') ? '|' : *sp;    // translate newline back to orig '|'
      sp++;
    }
    *reply = 0;  // set null terminator
  } else if (memcmp(config, "path.hash.mode", 14) == 0) {
    sprintf(reply, "> %d", (uint32_t)_prefs->path_hash_mode);
  } else if (memcmp(config, "loop.detect", 11) == 0) {
    if (_prefs->loop_detect == LOOP_DETECT_OFF) {
      strcpy(reply, "> off");
    } else if (_prefs->loop_detect == LOOP_DETECT_MINIMAL) {
      strcpy(reply, "> minimal");
    } else if (_prefs->loop_detect == LOOP_DETECT_MODERATE) {
      strcpy(reply, "> moderate");
    } else {
      strcpy(reply, "> strict");
    }
  } else if (memcmp(config, "tx", 2) == 0 && (config[2] == 0 || config[2] == ' ')) {
    sprintf(reply, "> %d", (int32_t) _prefs->tx_power_dbm);
  } else if (memcmp(config, "freq", 4) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->freq));
  } else if (memcmp(config, "public.key", 10) == 0) {
    strcpy(reply, "> ");
    mesh::Utils::toHex(&reply[2], _callbacks->getSelfId().pub_key, PUB_KEY_SIZE);
  } else if (memcmp(config, "role", 4) == 0) {
    sprintf(reply, "> %s", _callbacks->getRole());
  } else if (memcmp(config, "bridge.type", 11) == 0) {
    sprintf(reply, "> %s",
#ifdef WITH_RS232_BRIDGE
            "rs232"
#elif WITH_ESPNOW_BRIDGE
            "espnow"
#else
            "none"
#endif
    );
#ifdef WITH_BRIDGE
  } else if (memcmp(config, "bridge.enabled", 14) == 0) {
    sprintf(reply, "> %s", _prefs->bridge_enabled ? "on" : "off");
  } else if (memcmp(config, "bridge.delay", 12) == 0) {
    sprintf(reply, "> %d", (uint32_t)_prefs->bridge_delay);
  } else if (memcmp(config, "bridge.source", 13) == 0) {
    sprintf(reply, "> %s", _prefs->bridge_pkt_src ? "logRx" : "logTx");
#endif
#ifdef WITH_RS232_BRIDGE
  } else if (memcmp(config, "bridge.baud", 11) == 0) {
    sprintf(reply, "> %d", (uint32_t)_prefs->bridge_baud);
#endif
#ifdef WITH_ESPNOW_BRIDGE
  } else if (memcmp(config, "bridge.channel", 14) == 0) {
    sprintf(reply, "> %d", (uint32_t)_prefs->bridge_channel);
  } else if (memcmp(config, "bridge.secret", 13) == 0) {
    sprintf(reply, "> %s", _prefs->bridge_secret);
#endif
  } else if (memcmp(config, "bootloader.ver", 14) == 0) {
  #ifdef NRF52_PLATFORM
      char ver[32];
      if (_board->getBootloaderVersion(ver, sizeof(ver))) {
          sprintf(reply, "> %s", ver);
      } else {
          strcpy(reply, "> unknown");
      }
  #else
      strcpy(reply, "Error: unsupported");
  #endif
  } else if (memcmp(config, "adc.multiplier", 14) == 0) {
    float adc_mult = _board->getAdcMultiplier();
    if (adc_mult == 0.0f) {
      strcpy(reply, "Error: unsupported");
    } else {
      sprintf(reply, "> %.3f", adc_mult);
    }
  // Power management commands
  } else if (memcmp(config, "pwrmgt.support", 14) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
    strcpy(reply, "> supported");
#else
    strcpy(reply, "> unsupported");
#endif
  } else if (memcmp(config, "pwrmgt.source", 13) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
    strcpy(reply, _board->isExternalPowered() ? "> external" : "> battery");
#else
    strcpy(reply, "ERROR: Power management not supported");
#endif
  } else if (memcmp(config, "pwrmgt.bootreason", 17) == 0) {
    sprintf(reply, "> Reset: %s; Shutdown: %s",
      _board->getResetReasonString(_board->getResetReason()),
      _board->getShutdownReasonString(_board->getShutdownReason()));
  } else if (memcmp(config, "pwrmgt.bootmv", 13) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
    sprintf(reply, "> %u mV", _board->getBootVoltage());
#else
    strcpy(reply, "ERROR: Power management not supported");
#endif
  } else if (memcmp(config, "extra.sf", 8) == 0) {
    char* tmp = reply;
    for (int i = 0; i < 3 && _prefs->extra_sf[i] != 0; i++) {
      tmp += sprintf(tmp, "%s%d", (i == 0) ? "" : ",", _prefs->extra_sf[i]);
    } 
    if (tmp == reply) {
      sprintf(reply, "No extra SF configured");
    }
  } else {
    sprintf(reply, "??: %s", config);
  }
}

static char* skipSpaces(char* s) {
  while (*s == ' ') s++;
  return s;
}

static void rtrimSpaces(char* s) {
  char* e = s + strlen(s);
  while (e > s && e[-1] == ' ') *--e = '\0';
}

static char* takeToken(char** cursor) {
  char* p = skipSpaces(*cursor);
  if (*p == '\0') { *cursor = p; return nullptr; }
  char* tok = p;
  while (*p && *p != ' ') p++;
  if (*p) *p++ = '\0';
  *cursor = p;
  return tok;
}

static char* splitNameJump(char* tok) {
  for (char* q = tok; *q; q++) {
    if (*q == '|' || *q == ',') {
      *q = '\0';
      char* jump = skipSpaces(q + 1);
      rtrimSpaces(jump);
      return jump;
    }
  }
  return nullptr;
}

static bool processRegionDefSegment(RegionMap* map, char* tok, RegionEntry** cursor, char* reply) {
  char* jump = splitNameJump(tok);
  char* name = skipSpaces(tok);
  if (*name == '\0') { snprintf(reply, 160, "Err - empty name"); return false; }
  if (jump && *jump == '\0') { snprintf(reply, 160, "Err - empty jump"); return false; }

  RegionEntry* r = map->putRegion(name, (*cursor)->id);
  if (r == NULL) { snprintf(reply, 160, "Err - put failed: %s", name); return false; }
  r->flags = 0;

  if (jump) {
    RegionEntry* j = map->findByNamePrefix(jump);
    if (j == NULL) { snprintf(reply, 160, "Err - unknown jump: %s", jump); return false; }
    *cursor = j;
  } else {
    *cursor = r;
  }
  return true;
}

void CommonCLI::handleRegionCmd(char* command, char* reply) {
  reply[0] = 0;

  // `region def`: must run before parseTextParts mutates the buffer
  char* cmd = skipSpaces(command);
  if (strncmp(cmd, "region def", 10) == 0 && (cmd[10] == ' ' || cmd[10] == '\0')) {
    char* payload = skipSpaces(cmd + 10);
    rtrimSpaces(payload);
    if (*payload == '\0') { snprintf(reply, 160, "Err - empty def"); return; }

    RegionEntry* cursor = &_region_map->getWildcard();
    for (char* tok; (tok = takeToken(&payload)) != nullptr; ) {
      if (!processRegionDefSegment(_region_map, tok, &cursor, reply)) return;
    }
    _region_map->exportTo(reply, 160);
    return;
  }

  const char* parts[4];
  int n = mesh::Utils::parseTextParts(command, parts, 4, ' ');
  if (n == 1) {
    _region_map->exportTo(reply, 160);
  } else if (n >= 2 && strcmp(parts[1], "load") == 0) {
    _callbacks->startRegionsLoad();
  } else if (n >= 2 && strcmp(parts[1], "save") == 0) {
    _prefs->discovery_mod_timestamp = getRTCClock()->getCurrentTime();   // this node is now 'modified' (for discovery info)
    if (!savePrefs()) {
      formatPrefsSaveErr(reply);
    } else {
      bool success = _callbacks->saveRegions();
      strcpy(reply, success ? "OK" : "Err - save failed");
    }
  } else if (n >= 3 && strcmp(parts[1], "allowf") == 0) {
    auto region = _region_map->findByNamePrefix(parts[2]);
    if (region) {
      region->flags &= ~REGION_DENY_FLOOD;
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Err - unknown region");
    }
  } else if (n >= 3 && strcmp(parts[1], "denyf") == 0) {
    auto region = _region_map->findByNamePrefix(parts[2]);
    if (region) {
      region->flags |= REGION_DENY_FLOOD;
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Err - unknown region");
    }
  } else if (n >= 3 && strcmp(parts[1], "get") == 0) {
    auto region = _region_map->findByNamePrefix(parts[2]);
    if (region) {
      auto parent = _region_map->findById(region->parent);
      if (parent && parent->id != 0) {
        sprintf(reply, " %s (%s) %s", region->name, parent->name, (region->flags & REGION_DENY_FLOOD) ? "" : "F");
      } else {
        sprintf(reply, " %s %s", region->name, (region->flags & REGION_DENY_FLOOD) ? "" : "F");
      }
    } else {
      strcpy(reply, "Err - unknown region");
    }
  } else if (n >= 3 && strcmp(parts[1], "home") == 0) {
    auto home = _region_map->findByNamePrefix(parts[2]);
    if (home) {
      _region_map->setHomeRegion(home);
      sprintf(reply, " home is now %s", home->name);
    } else {
      strcpy(reply, "Err - unknown region");
    }
  } else if (n == 2 && strcmp(parts[1], "home") == 0) {
    auto home = _region_map->getHomeRegion();
    sprintf(reply, " home is %s", home ? home->name : "*");
  } else if (n >= 3 && strcmp(parts[1], "default") == 0) {
    if (strcmp(parts[2], "<null>") == 0) {
      _region_map->setDefaultRegion(NULL);
      _callbacks->onDefaultRegionChanged(NULL);
      _callbacks->saveRegions();  // persist in one atomic step
      sprintf(reply, " default scope is now <null>");
    } else {
      auto def = _region_map->findByNamePrefix(parts[2]);
      if (def == NULL) {
        def = _region_map->putRegion(parts[2], 0);  // auto-create the default region
      }
      if (def) {
        def->flags = 0;   // make sure allow flood enabled
        _region_map->setDefaultRegion(def);
        _callbacks->onDefaultRegionChanged(def);
        _callbacks->saveRegions();  // persist in one atomic step
        sprintf(reply, " default scope is now %s", def->name);
      } else {
        strcpy(reply, "Err - region table full");
      }
    }
  } else if (n == 2 && strcmp(parts[1], "default") == 0) {
    auto def = _region_map->getDefaultRegion();
    sprintf(reply, " default scope is %s", def ? def->name : "<null>");
  } else if (n >= 3 && strcmp(parts[1], "put") == 0) {
    auto parent = n >= 4 ? _region_map->findByNamePrefix(parts[3]) : &(_region_map->getWildcard());
    if (parent == NULL) {
      strcpy(reply, "Err - unknown parent");
    } else {
      auto region = _region_map->putRegion(parts[2], parent->id);
      if (region == NULL) {
        strcpy(reply, "Err - unable to put");
      } else {
        region->flags = 0;   // New default: enable flood
        strcpy(reply, "OK - (flood allowed)");
      }
    }
  } else if (n >= 3 && strcmp(parts[1], "remove") == 0) {
    auto region = _region_map->findByName(parts[2]);
    if (region) {
      if (_region_map->removeRegion(*region)) {
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "Err - not empty");
      }
    } else {
      strcpy(reply, "Err - not found");
    }
  } else if (n >= 3 && strcmp(parts[1], "list") == 0) {
    uint8_t mask = 0;
    bool invert = false;
    
    if (strcmp(parts[2], "allowed") == 0) {
      mask = REGION_DENY_FLOOD;
      invert = false;  // list regions that DON'T have DENY flag
    } else if (strcmp(parts[2], "denied") == 0) {
      mask = REGION_DENY_FLOOD;
      invert = true;   // list regions that DO have DENY flag
    } else {
      strcpy(reply, "Err - use 'allowed' or 'denied'");
      return;
    }
    
    int len = _region_map->exportNamesTo(reply, 160, mask, invert);
    if (len == 0) {
      strcpy(reply, "-none-");
    }
  } else {
    strcpy(reply, "Err - ??");
  }
}
