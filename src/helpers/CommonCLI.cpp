#include <Arduino.h>
#include "CommonCLI.h"
#include "TxtDataHelpers.h"
#include "AdvertDataHelpers.h"
#include <RTClib.h>
#include <string.h>
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#ifndef BRIDGE_MAX_BAUD
#define BRIDGE_MAX_BAUD 115200
#endif

// These bytes used to be reserved/unused in persisted prefs, so keep a marker before trusting them.
#define DIRECT_RETRY_PREFS_MAGIC_0  0xD4
#define DIRECT_RETRY_PREFS_MAGIC_1  0x52
#define DIRECT_RETRY_RECENT_DEFAULT          1
#define DIRECT_RETRY_SNR_MARGIN_DB_DEFAULT_X4  DIRECT_RETRY_ROOFTOP_MARGIN_X4
#define DIRECT_RETRY_SNR_MARGIN_DB_MAX       40
#define DIRECT_RETRY_SNR_MARGIN_X4_MAX      (DIRECT_RETRY_SNR_MARGIN_DB_MAX * 4)
#define DIRECT_RETRY_TIMING_MAGIC_0 0xD5
#define DIRECT_RETRY_TIMING_MAGIC_1 0x54
#define DIRECT_RETRY_COUNT_DEFAULT  DIRECT_RETRY_ROOFTOP_COUNT
#define DIRECT_RETRY_COUNT_MIN       1
#define DIRECT_RETRY_COUNT_MAX      15
#define DIRECT_RETRY_BASE_MS_DEFAULT DIRECT_RETRY_ROOFTOP_BASE_MS
#define DIRECT_RETRY_BASE_MS_MIN      10
#define DIRECT_RETRY_BASE_MS_MAX   5000
#define DIRECT_RETRY_STEP_MS_DEFAULT DIRECT_RETRY_ROOFTOP_STEP_MS
#define DIRECT_RETRY_STEP_MS_MIN       0
#define DIRECT_RETRY_STEP_MS_MAX    5000
#define DIRECT_RETRY_PRESET_DEFAULT DIRECT_RETRY_PRESET_ROOFTOP
#define FLOOD_RETRY_PREFS_MAGIC_0  0xF4
#define FLOOD_RETRY_PREFS_MAGIC_1  0x52
#define FLOOD_RETRY_COUNT_MIN       0
#define FLOOD_RETRY_COUNT_MAX       3

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

static bool parseUint8Strict(const char* value, uint8_t min_value, uint8_t max_value, uint8_t& result) {
  if (value == NULL || *value == 0) {
    return false;
  }

  uint16_t parsed = 0;
  const char* sp = value;
  while (*sp) {
    if (*sp < '0' || *sp > '9') {
      return false;
    }
    parsed = (uint16_t)((parsed * 10) + (*sp - '0'));
    if (parsed > max_value) {
      return false;
    }
    sp++;
  }
  if (parsed < min_value) {
    return false;
  }
  result = (uint8_t)parsed;
  return true;
}

static uint8_t directRetryMarginDbToX4(float margin_db) {
  int32_t scaled_x4 = (int32_t)((margin_db * 4.0f) + 0.5f);  // nearest 0.25 dB
  return (uint8_t)constrain(scaled_x4, 0, DIRECT_RETRY_SNR_MARGIN_X4_MAX);
}

static float directRetryMarginX4ToDb(uint8_t margin_x4) {
  return ((float)margin_x4) / 4.0f;
}

static uint8_t directRetryPresetOrDefault(uint8_t preset) {
  if (preset <= DIRECT_RETRY_PRESET_MOBILE) {
    return preset;
  }
  return DIRECT_RETRY_PRESET_DEFAULT;
}

static const char* directRetryPresetName(uint8_t preset) {
  switch (directRetryPresetOrDefault(preset)) {
    case DIRECT_RETRY_PRESET_INFRA:
      return "infra";
    case DIRECT_RETRY_PRESET_MOBILE:
      return "mobile";
    case DIRECT_RETRY_PRESET_ROOFTOP:
    default:
      return "rooftop";
  }
}

static uint8_t directRetryEffectiveCount(const NodePrefs* prefs) {
  return constrain(prefs->direct_retry_attempts, (uint8_t)DIRECT_RETRY_COUNT_MIN, (uint8_t)DIRECT_RETRY_COUNT_MAX);
}

static uint16_t directRetryEffectiveBaseMs(const NodePrefs* prefs) {
  return constrain(prefs->direct_retry_base_ms, (uint16_t)DIRECT_RETRY_BASE_MS_MIN, (uint16_t)DIRECT_RETRY_BASE_MS_MAX);
}

static uint16_t directRetryEffectiveStepMs(const NodePrefs* prefs) {
  return constrain(prefs->direct_retry_step_ms, (uint16_t)DIRECT_RETRY_STEP_MS_MIN, (uint16_t)DIRECT_RETRY_STEP_MS_MAX);
}

static uint8_t directRetryEffectiveMarginX4(const NodePrefs* prefs) {
  return constrain(prefs->direct_retry_snr_margin_db, (uint8_t)0, (uint8_t)DIRECT_RETRY_SNR_MARGIN_X4_MAX);
}

static uint16_t directRetryPresetStepDefault(uint8_t preset) {
  switch (directRetryPresetOrDefault(preset)) {
    case DIRECT_RETRY_PRESET_INFRA:
      return DIRECT_RETRY_INFRA_STEP_MS;
    case DIRECT_RETRY_PRESET_MOBILE:
      return DIRECT_RETRY_MOBILE_STEP_MS;
    case DIRECT_RETRY_PRESET_ROOFTOP:
    default:
      return DIRECT_RETRY_ROOFTOP_STEP_MS;
  }
}

static uint8_t floodRetryPresetCountDefault(uint8_t preset) {
  switch (directRetryPresetOrDefault(preset)) {
    case DIRECT_RETRY_PRESET_INFRA:
      return 1;
    case DIRECT_RETRY_PRESET_MOBILE:
    case DIRECT_RETRY_PRESET_ROOFTOP:
    default:
      return 3;
  }
}

static uint8_t floodRetryPresetPathDefault(uint8_t preset) {
  switch (directRetryPresetOrDefault(preset)) {
    case DIRECT_RETRY_PRESET_INFRA:
      return 1;
    case DIRECT_RETRY_PRESET_MOBILE:
      return 1;
    case DIRECT_RETRY_PRESET_ROOFTOP:
    default:
      return 2;
  }
}

static void applyFloodRetryPreset(NodePrefs* prefs, uint8_t preset) {
  prefs->flood_retry_attempts = floodRetryPresetCountDefault(preset);
  prefs->flood_retry_path_gate = floodRetryPresetPathDefault(preset);
}

static uint8_t floodRetryEffectiveCount(const NodePrefs* prefs) {
  return constrain(prefs->flood_retry_attempts, (uint8_t)FLOOD_RETRY_COUNT_MIN, (uint8_t)FLOOD_RETRY_COUNT_MAX);
}

static uint8_t floodRetryPresetForPrefs(const NodePrefs* prefs) {
  uint8_t count = floodRetryEffectiveCount(prefs);
  uint8_t path_gate = prefs->flood_retry_path_gate;
  for (uint8_t preset = DIRECT_RETRY_PRESET_INFRA; preset <= DIRECT_RETRY_PRESET_MOBILE; preset++) {
    if (count == floodRetryPresetCountDefault(preset) && path_gate == floodRetryPresetPathDefault(preset)) {
      return preset;
    }
  }
  return directRetryPresetOrDefault(prefs->direct_retry_preset);
}

static void applyDirectRetryPreset(NodePrefs* prefs, uint8_t preset) {
  prefs->direct_retry_preset = directRetryPresetOrDefault(preset);
  switch (prefs->direct_retry_preset) {
    case DIRECT_RETRY_PRESET_INFRA:
      prefs->direct_retry_base_ms = DIRECT_RETRY_INFRA_BASE_MS;
      prefs->direct_retry_attempts = DIRECT_RETRY_INFRA_COUNT;
      prefs->direct_retry_step_ms = DIRECT_RETRY_INFRA_STEP_MS;
      prefs->direct_retry_snr_margin_db = DIRECT_RETRY_INFRA_MARGIN_X4;
      break;
    case DIRECT_RETRY_PRESET_MOBILE:
      prefs->direct_retry_base_ms = DIRECT_RETRY_MOBILE_BASE_MS;
      prefs->direct_retry_attempts = DIRECT_RETRY_MOBILE_COUNT;
      prefs->direct_retry_step_ms = DIRECT_RETRY_MOBILE_STEP_MS;
      prefs->direct_retry_snr_margin_db = DIRECT_RETRY_MOBILE_MARGIN_X4;
      break;
    case DIRECT_RETRY_PRESET_ROOFTOP:
    default:
      prefs->direct_retry_base_ms = DIRECT_RETRY_ROOFTOP_BASE_MS;
      prefs->direct_retry_attempts = DIRECT_RETRY_ROOFTOP_COUNT;
      prefs->direct_retry_step_ms = DIRECT_RETRY_ROOFTOP_STEP_MS;
      prefs->direct_retry_snr_margin_db = DIRECT_RETRY_ROOFTOP_MARGIN_X4;
      break;
  }
  applyFloodRetryPreset(prefs, prefs->direct_retry_preset);
}

static bool parseFloodRetryPathGate(const char* value, uint8_t& path_gate) {
  if (value == NULL) {
    return false;
  }
  if (strcmp(value, "off") == 0 || strcmp(value, "disabled") == 0 || strcmp(value, "disable") == 0) {
    path_gate = FLOOD_RETRY_PATH_GATE_DISABLED;
    return true;
  }
  return parseUint8Strict(value, 0, 63, path_gate);
}

static void formatFloodRetryPathGate(char* dest, uint8_t path_gate) {
  if (path_gate == FLOOD_RETRY_PATH_GATE_DISABLED) {
    strcpy(dest, "off");
  } else {
    sprintf(dest, "%u", (unsigned int)path_gate);
  }
}

static void formatFloodRetryPrefixList(char* dest, const uint8_t prefixes[][FLOOD_RETRY_PREFIX_LEN],
                                       uint8_t max_prefixes) {
  char* out = dest;
  bool first = true;
  for (int i = 0; i < max_prefixes; i++) {
    const uint8_t* prefix = prefixes[i];
    if (prefix[0] == 0 && prefix[1] == 0 && prefix[2] == 0) {
      continue;
    }
    if (!first) {
      *out++ = ',';
    }
    mesh::Utils::toHex(out, prefix, FLOOD_RETRY_PREFIX_LEN);
    out += FLOOD_RETRY_PREFIX_LEN * 2;
    first = false;
  }
  *out = 0;
}

static void formatFloodRetryPrefixes(char* dest, const NodePrefs* prefs) {
  formatFloodRetryPrefixList(dest, prefs->flood_retry_prefixes, FLOOD_RETRY_PREFIX_SLOTS);
}

static void formatFloodRetryBridgeBucket(char* dest, const NodePrefs* prefs, uint8_t bucket) {
  if (bucket >= FLOOD_RETRY_BRIDGE_BUCKETS) {
    dest[0] = 0;
    return;
  }
  formatFloodRetryPrefixList(dest, prefs->flood_retry_bridge_buckets[bucket], FLOOD_RETRY_BUCKET_PREFIXES);
}

static bool parseFloodRetryPrefixes(NodePrefs* prefs, const char* value) {
  uint8_t parsed[FLOOD_RETRY_PREFIX_SLOTS][FLOOD_RETRY_PREFIX_LEN];
  memset(parsed, 0, sizeof(parsed));
  if (value == NULL || value[0] == 0 || strcmp(value, "none") == 0 || strcmp(value, "off") == 0) {
    memcpy(prefs->flood_retry_prefixes, parsed, sizeof(prefs->flood_retry_prefixes));
    return true;
  }

  char tmp[FLOOD_RETRY_LIST_TEXT_MAX];
  StrHelper::strncpy(tmp, value, sizeof(tmp));
  const char* parts[FLOOD_RETRY_PREFIX_SLOTS + 1];
  int num = mesh::Utils::parseTextParts(tmp, parts, FLOOD_RETRY_PREFIX_SLOTS + 1);
  if (num > FLOOD_RETRY_PREFIX_SLOTS) {
    return false;
  }
  for (int i = 0; i < num; i++) {
    if (strlen(parts[i]) != FLOOD_RETRY_PREFIX_LEN * 2) {
      return false;
    }
    for (int j = 0; j < FLOOD_RETRY_PREFIX_LEN * 2; j++) {
      if (!mesh::Utils::isHexChar(parts[i][j])) {
        return false;
      }
    }
    if (!mesh::Utils::fromHex(parsed[i], FLOOD_RETRY_PREFIX_LEN, parts[i])) {
      return false;
    }
    const uint8_t* prefix = parsed[i];
    if (prefix[0] == 0 && prefix[1] == 0 && prefix[2] == 0) {
      return false;
    }
  }
  memcpy(prefs->flood_retry_prefixes, parsed, sizeof(prefs->flood_retry_prefixes));
  return true;
}

static bool parseFloodRetryPrefixList(uint8_t dest[][FLOOD_RETRY_PREFIX_LEN], uint8_t max_prefixes, const char* value) {
  if (max_prefixes > FLOOD_RETRY_LIST_PREFIXES) {
    return false;
  }
  uint8_t parsed[FLOOD_RETRY_LIST_PREFIXES][FLOOD_RETRY_PREFIX_LEN];
  memset(parsed, 0, sizeof(parsed));
  if (value == NULL || value[0] == 0 || strcmp(value, "none") == 0 || strcmp(value, "off") == 0) {
    memcpy(dest, parsed, max_prefixes * FLOOD_RETRY_PREFIX_LEN);
    return true;
  }

  char tmp[FLOOD_RETRY_LIST_TEXT_MAX];
  StrHelper::strncpy(tmp, value, sizeof(tmp));
  const char* parts[FLOOD_RETRY_LIST_PREFIXES + 1];
  int num = mesh::Utils::parseTextParts(tmp, parts, FLOOD_RETRY_LIST_PREFIXES + 1);
  if (num > max_prefixes) {
    return false;
  }
  for (int i = 0; i < num; i++) {
    if (strlen(parts[i]) != FLOOD_RETRY_PREFIX_LEN * 2) {
      return false;
    }
    for (int j = 0; j < FLOOD_RETRY_PREFIX_LEN * 2; j++) {
      if (!mesh::Utils::isHexChar(parts[i][j])) {
        return false;
      }
    }
    if (!mesh::Utils::fromHex(parsed[i], FLOOD_RETRY_PREFIX_LEN, parts[i])
        || (parsed[i][0] == 0 && parsed[i][1] == 0 && parsed[i][2] == 0)) {
      return false;
    }
  }
  memcpy(dest, parsed, max_prefixes * FLOOD_RETRY_PREFIX_LEN);
  return true;
}

static bool parseDirectRetryPreset(const char* value, uint8_t& preset) {
  if (value == NULL) {
    return false;
  }
  if (strcmp(value, "0") == 0 || strcmp(value, "infra") == 0
      || strcmp(value, "infa") == 0 || strcmp(value, "infrastructure") == 0) {
    preset = DIRECT_RETRY_PRESET_INFRA;
    return true;
  }
  if (strcmp(value, "1") == 0 || strcmp(value, "rooftop") == 0) {
    preset = DIRECT_RETRY_PRESET_ROOFTOP;
    return true;
  }
  if (strcmp(value, "2") == 0 || strcmp(value, "mobile") == 0) {
    preset = DIRECT_RETRY_PRESET_MOBILE;
    return true;
  }
  return false;
}

static bool isValidName(const char *n) {
  while (*n) {
    if (*n == '[' || *n == ']' || *n == '\\' || *n == ':' || *n == ',' || *n == '?' || *n == '*') return false;
    n++;
  }
  return true;
}

void CommonCLI::loadPrefs(FILESYSTEM* fs) {
  if (fs->exists("/com_prefs")) {
    loadPrefsInt(fs, "/com_prefs");   // new filename
  } else if (fs->exists("/node_prefs")) {
    loadPrefsInt(fs, "/node_prefs");
    savePrefs(fs);  // save to new filename
    fs->remove("/node_prefs");  // remove old
  }
}

void CommonCLI::loadPrefsInt(FILESYSTEM* fs, const char* filename) {
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
    file.read((uint8_t *)&_prefs->direct_retry_recent_enabled, sizeof(_prefs->direct_retry_recent_enabled)); // 108
    file.read((uint8_t *)&_prefs->direct_retry_snr_margin_db, sizeof(_prefs->direct_retry_snr_margin_db));   // 109
    file.read((uint8_t *)&_prefs->direct_retry_prefs_magic[0], sizeof(_prefs->direct_retry_prefs_magic));    // 110
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
    file.read((uint8_t *)&_prefs->rx_boosted_gain, sizeof(_prefs->rx_boosted_gain));              // 290
    uint8_t legacy_retry_attempts_or_radio_fem_rxgain = _prefs->direct_retry_attempts;
    size_t legacy_retry_attempts_read = file.read((uint8_t *)&legacy_retry_attempts_or_radio_fem_rxgain,
                                                  sizeof(legacy_retry_attempts_or_radio_fem_rxgain)); // 291
    if (legacy_retry_attempts_read == sizeof(legacy_retry_attempts_or_radio_fem_rxgain)) {
      _prefs->direct_retry_attempts = legacy_retry_attempts_or_radio_fem_rxgain;
    }
    file.read((uint8_t *)&_prefs->direct_retry_base_ms, sizeof(_prefs->direct_retry_base_ms));    // 292
    file.read((uint8_t *)&_prefs->direct_retry_timing_magic[0], sizeof(_prefs->direct_retry_timing_magic)); // 294
    size_t radio_fem_rxgain_read = file.read((uint8_t *)&_prefs->radio_fem_rxgain,
                                             sizeof(_prefs->radio_fem_rxgain));                  // 296
    file.read((uint8_t *)&_prefs->direct_retry_preset, sizeof(_prefs->direct_retry_preset));     // 297
    size_t retry_step_read = file.read((uint8_t *)&_prefs->direct_retry_step_ms,
                                       sizeof(_prefs->direct_retry_step_ms));                    // 298
    size_t flood_retry_attempts_read = file.read((uint8_t *)&_prefs->flood_retry_attempts,
                                                 sizeof(_prefs->flood_retry_attempts));          // 300
    file.read((uint8_t *)&_prefs->flood_retry_path_gate, sizeof(_prefs->flood_retry_path_gate)); // 301
    file.read((uint8_t *)&_prefs->flood_retry_prefs_magic[0], sizeof(_prefs->flood_retry_prefs_magic)); // 302
    file.read((uint8_t *)&_prefs->flood_retry_prefixes[0][0], sizeof(_prefs->flood_retry_prefixes)); // 304
    file.read((uint8_t *)&_prefs->flood_retry_bridge_enabled, sizeof(_prefs->flood_retry_bridge_enabled)); // 328
    memset(_prefs->flood_retry_bridge_buckets, 0, sizeof(_prefs->flood_retry_bridge_buckets));
    memset(_prefs->flood_retry_ignore_prefixes, 0, sizeof(_prefs->flood_retry_ignore_prefixes));
    file.read((uint8_t *)&_prefs->flood_retry_bridge_buckets[0][0][0], sizeof(_prefs->flood_retry_bridge_buckets)); // 329
    size_t flood_retry_ignore_read = file.read((uint8_t *)&_prefs->flood_retry_ignore_prefixes[0][0],
                                               sizeof(_prefs->flood_retry_ignore_prefixes)); // 635
    // PowerSaving-only prefs stored radio_fem_rxgain at 291, before direct retry timing existed.
    if (radio_fem_rxgain_read != sizeof(_prefs->radio_fem_rxgain)
        && legacy_retry_attempts_read == sizeof(legacy_retry_attempts_or_radio_fem_rxgain)
        && (_prefs->direct_retry_timing_magic[0] != DIRECT_RETRY_TIMING_MAGIC_0
            || _prefs->direct_retry_timing_magic[1] != DIRECT_RETRY_TIMING_MAGIC_1)) {
      _prefs->radio_fem_rxgain = constrain(legacy_retry_attempts_or_radio_fem_rxgain, 0, 1);
    }
    // next: 659

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
    // Old firmware left offset 108..111 undefined, so require the marker before using the new retry prefs.
    if (_prefs->direct_retry_prefs_magic[0] != DIRECT_RETRY_PREFS_MAGIC_0
        || _prefs->direct_retry_prefs_magic[1] != DIRECT_RETRY_PREFS_MAGIC_1) {
      _prefs->direct_retry_recent_enabled = DIRECT_RETRY_RECENT_DEFAULT;
      _prefs->direct_retry_snr_margin_db = DIRECT_RETRY_SNR_MARGIN_DB_DEFAULT_X4;
    } else {
      _prefs->direct_retry_recent_enabled = constrain(_prefs->direct_retry_recent_enabled, 0, 1);
      _prefs->direct_retry_snr_margin_db = constrain(_prefs->direct_retry_snr_margin_db, 0, DIRECT_RETRY_SNR_MARGIN_X4_MAX);
    }
    if (_prefs->direct_retry_timing_magic[0] != DIRECT_RETRY_TIMING_MAGIC_0
        || _prefs->direct_retry_timing_magic[1] != DIRECT_RETRY_TIMING_MAGIC_1) {
      _prefs->direct_retry_attempts = DIRECT_RETRY_COUNT_DEFAULT;
      _prefs->direct_retry_base_ms = DIRECT_RETRY_BASE_MS_DEFAULT;
    } else {
      _prefs->direct_retry_attempts = constrain(_prefs->direct_retry_attempts, DIRECT_RETRY_COUNT_MIN, DIRECT_RETRY_COUNT_MAX);
      _prefs->direct_retry_base_ms = constrain(_prefs->direct_retry_base_ms, DIRECT_RETRY_BASE_MS_MIN, DIRECT_RETRY_BASE_MS_MAX);
    }

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
    _prefs->direct_retry_preset = directRetryPresetOrDefault(_prefs->direct_retry_preset);
    if (retry_step_read != sizeof(_prefs->direct_retry_step_ms)) {
      _prefs->direct_retry_step_ms = directRetryPresetStepDefault(_prefs->direct_retry_preset);
    } else {
      _prefs->direct_retry_step_ms = constrain(_prefs->direct_retry_step_ms, DIRECT_RETRY_STEP_MS_MIN, DIRECT_RETRY_STEP_MS_MAX);
    }
    if (flood_retry_attempts_read != sizeof(_prefs->flood_retry_attempts)
        || _prefs->flood_retry_prefs_magic[0] != FLOOD_RETRY_PREFS_MAGIC_0
        || _prefs->flood_retry_prefs_magic[1] != FLOOD_RETRY_PREFS_MAGIC_1) {
      applyFloodRetryPreset(_prefs, _prefs->direct_retry_preset);
      memset(_prefs->flood_retry_prefixes, 0, sizeof(_prefs->flood_retry_prefixes));
      _prefs->flood_retry_bridge_enabled = 0;
      memset(_prefs->flood_retry_bridge_buckets, 0, sizeof(_prefs->flood_retry_bridge_buckets));
      memset(_prefs->flood_retry_ignore_prefixes, 0, sizeof(_prefs->flood_retry_ignore_prefixes));
    } else {
      _prefs->flood_retry_attempts = constrain(_prefs->flood_retry_attempts, FLOOD_RETRY_COUNT_MIN, FLOOD_RETRY_COUNT_MAX);
      if (_prefs->flood_retry_path_gate > 63 && _prefs->flood_retry_path_gate != FLOOD_RETRY_PATH_GATE_DISABLED) {
        _prefs->flood_retry_path_gate = floodRetryPresetPathDefault(_prefs->direct_retry_preset);
      }
      _prefs->flood_retry_bridge_enabled = constrain(_prefs->flood_retry_bridge_enabled, 0, 1);
      if (flood_retry_ignore_read != sizeof(_prefs->flood_retry_ignore_prefixes)) {
        memset(_prefs->flood_retry_ignore_prefixes, 0, sizeof(_prefs->flood_retry_ignore_prefixes));
      }
    }

    file.close();
  }
}

void CommonCLI::savePrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/com_prefs");
  File file = fs->open("/com_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open("/com_prefs", "w");
#else
  File file = fs->open("/com_prefs", "w", true);
#endif
  if (file) {
    uint8_t pad[8];
    memset(pad, 0, sizeof(pad));

    file.write((uint8_t *)&_prefs->airtime_factor, sizeof(_prefs->airtime_factor));    // 0
    file.write((uint8_t *)&_prefs->node_name, sizeof(_prefs->node_name));              // 4
    file.write(pad, 4);                                                                // 36
    file.write((uint8_t *)&_prefs->node_lat, sizeof(_prefs->node_lat));                // 40
    file.write((uint8_t *)&_prefs->node_lon, sizeof(_prefs->node_lon));                // 48
    file.write((uint8_t *)&_prefs->password[0], sizeof(_prefs->password));             // 56
    file.write((uint8_t *)&_prefs->freq, sizeof(_prefs->freq));                        // 72
    file.write((uint8_t *)&_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));        // 76
    file.write((uint8_t *)&_prefs->disable_fwd, sizeof(_prefs->disable_fwd));          // 77
    file.write((uint8_t *)&_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.write(pad, 1);                                                                // 79 : 1 byte unused (rx_boosted_gain moved to end)
    file.write((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.write((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.write((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.write((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.write((uint8_t *)&_prefs->direct_retry_recent_enabled, sizeof(_prefs->direct_retry_recent_enabled)); // 108
    file.write((uint8_t *)&_prefs->direct_retry_snr_margin_db, sizeof(_prefs->direct_retry_snr_margin_db));   // 109
    // Persist a marker so later loads can distinguish real values from legacy garbage in this reserved slot.
    uint8_t retry_magic[2] = { DIRECT_RETRY_PREFS_MAGIC_0, DIRECT_RETRY_PREFS_MAGIC_1 };
    file.write(retry_magic, sizeof(retry_magic)); // 110
    file.write((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.write((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.write((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.write((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.write((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.write((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.write((uint8_t *)&_prefs->path_hash_mode, sizeof(_prefs->path_hash_mode));                 // 121
    file.write((uint8_t *)&_prefs->loop_detect, sizeof(_prefs->loop_detect));                       // 122
    file.write(pad, 1);                                                                             // 123
    file.write((uint8_t *)&_prefs->flood_max, sizeof(_prefs->flood_max));                           // 124
    file.write((uint8_t *)&_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));   // 125
    file.write((uint8_t *)&_prefs->interference_threshold, sizeof(_prefs->interference_threshold)); // 126
    file.write((uint8_t *)&_prefs->bridge_enabled, sizeof(_prefs->bridge_enabled));                 // 127
    file.write((uint8_t *)&_prefs->bridge_delay, sizeof(_prefs->bridge_delay));                     // 128
    file.write((uint8_t *)&_prefs->bridge_pkt_src, sizeof(_prefs->bridge_pkt_src));                 // 130
    file.write((uint8_t *)&_prefs->bridge_baud, sizeof(_prefs->bridge_baud));                       // 131
    file.write((uint8_t *)&_prefs->bridge_channel, sizeof(_prefs->bridge_channel));                 // 135
    file.write((uint8_t *)&_prefs->bridge_secret, sizeof(_prefs->bridge_secret));                   // 136
    file.write((uint8_t *)&_prefs->powersaving_enabled, sizeof(_prefs->powersaving_enabled));       // 152
    file.write(pad, 3);                                                                             // 153
    file.write((uint8_t *)&_prefs->gps_enabled, sizeof(_prefs->gps_enabled));                       // 156
    file.write((uint8_t *)&_prefs->gps_interval, sizeof(_prefs->gps_interval));                     // 157
    file.write((uint8_t *)&_prefs->advert_loc_policy, sizeof(_prefs->advert_loc_policy));           // 161
    file.write((uint8_t *)&_prefs->discovery_mod_timestamp, sizeof(_prefs->discovery_mod_timestamp)); // 162
    file.write((uint8_t *)&_prefs->adc_multiplier, sizeof(_prefs->adc_multiplier));                 // 166
    file.write((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));                          // 170
    file.write((uint8_t *)&_prefs->rx_boosted_gain, sizeof(_prefs->rx_boosted_gain));              // 290
    file.write((uint8_t *)&_prefs->direct_retry_attempts, sizeof(_prefs->direct_retry_attempts));  // 291
    file.write((uint8_t *)&_prefs->direct_retry_base_ms, sizeof(_prefs->direct_retry_base_ms));    // 292
    uint8_t retry_timing_magic[2] = { DIRECT_RETRY_TIMING_MAGIC_0, DIRECT_RETRY_TIMING_MAGIC_1 };
    file.write(retry_timing_magic, sizeof(retry_timing_magic));                                     // 294
    file.write((uint8_t *)&_prefs->radio_fem_rxgain, sizeof(_prefs->radio_fem_rxgain));            // 296
    file.write((uint8_t *)&_prefs->direct_retry_preset, sizeof(_prefs->direct_retry_preset));       // 297
    file.write((uint8_t *)&_prefs->direct_retry_step_ms, sizeof(_prefs->direct_retry_step_ms));     // 298
    file.write((uint8_t *)&_prefs->flood_retry_attempts, sizeof(_prefs->flood_retry_attempts));     // 300
    file.write((uint8_t *)&_prefs->flood_retry_path_gate, sizeof(_prefs->flood_retry_path_gate));   // 301
    uint8_t flood_retry_magic[2] = { FLOOD_RETRY_PREFS_MAGIC_0, FLOOD_RETRY_PREFS_MAGIC_1 };
    file.write(flood_retry_magic, sizeof(flood_retry_magic));                                       // 302
    file.write((uint8_t *)&_prefs->flood_retry_prefixes[0][0], sizeof(_prefs->flood_retry_prefixes)); // 304
    file.write((uint8_t *)&_prefs->flood_retry_bridge_enabled, sizeof(_prefs->flood_retry_bridge_enabled)); // 328
    file.write((uint8_t *)&_prefs->flood_retry_bridge_buckets[0][0][0], sizeof(_prefs->flood_retry_bridge_buckets)); // 329
    file.write((uint8_t *)&_prefs->flood_retry_ignore_prefixes[0][0], sizeof(_prefs->flood_retry_ignore_prefixes)); // 635
    // next: 659

    file.close();
  }
}

#define MIN_LOCAL_ADVERT_INTERVAL   60

void CommonCLI::savePrefs() {
  if (_prefs->advert_interval * 2 < MIN_LOCAL_ADVERT_INTERVAL) {
    _prefs->advert_interval = 0;  // turn it off, now that device has been manually configured
  }
  _callbacks->savePrefs();
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
      sprintf(reply, "%02d:%02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(), dt.year());
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
      savePrefs();
      sprintf(reply, "password now: %s", _prefs->password);   // echo back just to let admin know for sure!!
    } else if (memcmp(command, "clear stats", 11) == 0) {
      _callbacks->clearStats();
      strcpy(reply, "(OK - stats reset)");
    } else if (memcmp(command, "get ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af", 2) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->airtime_factor));
      } else if (memcmp(config, "int.thresh", 10) == 0) {
        sprintf(reply, "> %d", (uint32_t) _prefs->interference_threshold);
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
#if defined(USE_SX1262) || defined(USE_SX1268)
      } else if (memcmp(config, "radio.rxgain", 12) == 0) {
        sprintf(reply, "> %s", _prefs->rx_boosted_gain ? "on" : "off");
#endif
      } else if (memcmp(config, "radio", 5) == 0) {
        char freq[16], bw[16];
        strcpy(freq, StrHelper::ftoa(_prefs->freq));
        strcpy(bw, StrHelper::ftoa3(_prefs->bw));
        sprintf(reply, "> %s,%s,%d,%d", freq, bw, (uint32_t)_prefs->sf, (uint32_t)_prefs->cr);
      } else if (memcmp(config, "rxdelay", 7) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->rx_delay_base));
      } else if (memcmp(config, "txdelay", 7) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->tx_delay_factor));
      } else if (memcmp(config, "flood.max", 9) == 0) {
        sprintf(reply, "> %d", (uint32_t)_prefs->flood_max);
      } else if (memcmp(config, "flood.retry.count", 17) == 0) {
        sprintf(reply, "> %d", (uint32_t)floodRetryEffectiveCount(_prefs));
      } else if (memcmp(config, "flood.retry.path", 16) == 0) {
        char path_gate[8];
        formatFloodRetryPathGate(path_gate, _prefs->flood_retry_path_gate);
        sprintf(reply, "> %s", path_gate);
      } else if (memcmp(config, "flood.retry.prefixes", 20) == 0) {
        formatFloodRetryPrefixes(tmp, _prefs);
        sprintf(reply, "> %s", tmp[0] ? tmp : "none");
      } else if (memcmp(config, "flood.retry.ignore", 18) == 0) {
        formatFloodRetryPrefixList(tmp, _prefs->flood_retry_ignore_prefixes, FLOOD_RETRY_IGNORE_PREFIXES);
        sprintf(reply, "> %s", tmp[0] ? tmp : "none");
      } else if (memcmp(config, "flood.retry.bridge", 18) == 0) {
        sprintf(reply, "> %s", _prefs->flood_retry_bridge_enabled ? "on" : "off");
      } else if (memcmp(config, "flood.retry.bucket.", 19) == 0) {
        uint8_t bucket = atoi(&config[19]);
        if (bucket >= 1 && bucket <= FLOOD_RETRY_BRIDGE_BUCKETS) {
          formatFloodRetryBridgeBucket(tmp, _prefs, bucket - 1);
          sprintf(reply, "> %s", tmp[0] ? tmp : "none");
        } else {
          sprintf(reply, "Error, bucket 1-%d", FLOOD_RETRY_BRIDGE_BUCKETS);
        }
      } else if (memcmp(config, "flood.retry.preset", 18) == 0) {
        uint8_t preset = floodRetryPresetForPrefs(_prefs);
        sprintf(reply, "> %d,%s",
                (uint32_t)preset,
                directRetryPresetName(preset));
      } else if (memcmp(config, "direct.txdelay", 14) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->direct_tx_delay_factor));
      } else if (memcmp(config, "direct.retry.heard", 18) == 0) {
        sprintf(reply, "> %s", _prefs->direct_retry_recent_enabled ? "on" : "off");
      } else if (memcmp(config, "direct.retry.margin", 19) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(directRetryMarginX4ToDb(directRetryEffectiveMarginX4(_prefs))));
      } else if (memcmp(config, "direct.retry.preset", 19) == 0) {
        sprintf(reply, "> %d,%s",
                (uint32_t)directRetryPresetOrDefault(_prefs->direct_retry_preset),
                directRetryPresetName(_prefs->direct_retry_preset));
      } else if (memcmp(config, "direct.retry.count", 18) == 0) {
        sprintf(reply, "> %d", (uint32_t)directRetryEffectiveCount(_prefs));
      } else if (memcmp(config, "direct.retry.base", 17) == 0) {
        sprintf(reply, "> %d", (uint32_t)directRetryEffectiveBaseMs(_prefs));
      } else if (memcmp(config, "direct.retry.step", 17) == 0) {
        sprintf(reply, "> %d", (uint32_t)directRetryEffectiveStepMs(_prefs));
      } else if (memcmp(config, "owner.info", 10) == 0) {
        *reply++ = '>';
        *reply++ = ' ';
        const char* sp = _prefs->owner_info;
        while (*sp) {
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
          strcpy(reply, "ERROR: unsupported");
      #endif
      } else if (memcmp(config, "adc.multiplier", 14) == 0) {
        float adc_mult = _board->getAdcMultiplier();
        if (adc_mult == 0.0f) {
          strcpy(reply, "Error: unsupported by this board");
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
#ifdef NRF52_POWER_MANAGEMENT
        sprintf(reply, "> Reset: %s; Shutdown: %s",
          _board->getResetReasonString(_board->getResetReason()),
          _board->getShutdownReasonString(_board->getShutdownReason()));
#else
        strcpy(reply, "ERROR: Power management not supported");
#endif
      } else if (memcmp(config, "pwrmgt.bootmv", 13) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
        sprintf(reply, "> %u mV", _board->getBootVoltage());
#else
        strcpy(reply, "ERROR: Power management not supported");
#endif
      } else {
        sprintf(reply, "??: %s", config);
      }
    /*
     * SET commands
     */
    } else if (memcmp(command, "set ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af ", 3) == 0) {
        _prefs->airtime_factor = atof(&config[3]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "int.thresh ", 11) == 0) {
        _prefs->interference_threshold = atoi(&config[11]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "agc.reset.interval ", 19) == 0) {
        _prefs->agc_reset_interval = atoi(&config[19]) / 4;
        savePrefs();
        sprintf(reply, "OK - interval rounded to %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
      } else if (memcmp(config, "multi.acks ", 11) == 0) {
        _prefs->multi_acks = atoi(&config[11]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "allow.read.only ", 16) == 0) {
        _prefs->allow_read_only = memcmp(&config[16], "on", 2) == 0;
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "flood.advert.interval ", 22) == 0) {
        int hours = _atoi(&config[22]);
        if ((hours > 0 && hours < 3) || (hours > 168)) {
          strcpy(reply, "Error: interval range is 3-168 hours");
        } else {
          _prefs->flood_advert_interval = (uint8_t)(hours);
          _callbacks->updateFloodAdvertTimer();
          savePrefs();
          strcpy(reply, "OK");
        }
      } else if (memcmp(config, "advert.interval ", 16) == 0) {
        int mins = _atoi(&config[16]);
        if ((mins > 0 && mins < MIN_LOCAL_ADVERT_INTERVAL) || (mins > 240)) {
          sprintf(reply, "Error: interval range is %d-240 minutes", MIN_LOCAL_ADVERT_INTERVAL);
        } else {
          _prefs->advert_interval = (uint8_t)(mins / 2);
          _callbacks->updateAdvertTimer();
          savePrefs();
          strcpy(reply, "OK");
        }
      } else if (memcmp(config, "guest.password ", 15) == 0) {
        StrHelper::strncpy(_prefs->guest_password, &config[15], sizeof(_prefs->guest_password));
        savePrefs();
        strcpy(reply, "OK");
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
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, bad chars");
        }
      } else if (memcmp(config, "repeat ", 7) == 0) {
        _prefs->disable_fwd = memcmp(&config[7], "off", 3) == 0;
        savePrefs();
        strcpy(reply, _prefs->disable_fwd ? "OK - repeat is now OFF" : "OK - repeat is now ON");
#if defined(USE_SX1262) || defined(USE_SX1268)
      } else if (memcmp(config, "radio.rxgain ", 13) == 0) {
        _prefs->rx_boosted_gain = memcmp(&config[13], "on", 2) == 0;
        strcpy(reply, "OK");
        savePrefs();
        _callbacks->setRxBoostedGain(_prefs->rx_boosted_gain);
#endif
      } else if (memcmp(config, "radio ", 6) == 0) {
        strcpy(tmp, &config[6]);
        const char *parts[4];
        int num = mesh::Utils::parseTextParts(tmp, parts, 4);
        float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
        float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
        uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
        uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
        if (freq >= 300.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
          _prefs->sf = sf;
          _prefs->cr = cr;
          _prefs->freq = freq;
          _prefs->bw = bw;
          _callbacks->savePrefs();
          strcpy(reply, "OK - reboot to apply");
        } else {
          strcpy(reply, "Error, invalid radio params");
        }
      } else if (memcmp(config, "lat ", 4) == 0) {
        _prefs->node_lat = atof(&config[4]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "lon ", 4) == 0) {
        _prefs->node_lon = atof(&config[4]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "rxdelay ", 8) == 0) {
        float db = atof(&config[8]);
        if (db >= 0) {
          _prefs->rx_delay_base = db;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, "txdelay ", 8) == 0) {
        float f = atof(&config[8]);
        if (f >= 0) {
          _prefs->tx_delay_factor = f;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, "flood.max ", 10) == 0) {
        uint8_t m = atoi(&config[10]);
        if (m <= 64) {
          _prefs->flood_max = m;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, max 64");
        }
      } else if (memcmp(config, "flood.retry.preset ", 19) == 0) {
        uint8_t preset;
        if (parseDirectRetryPreset(&config[19], preset)) {
          applyFloodRetryPreset(_prefs, preset);
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, must be infra, rooftop, mobile, 0, 1, or 2");
        }
      } else if (memcmp(config, "flood.retry.count ", 18) == 0) {
        uint8_t count;
        if (parseUint8Strict(&config[18], FLOOD_RETRY_COUNT_MIN, FLOOD_RETRY_COUNT_MAX, count)) {
          _prefs->flood_retry_attempts = count;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          sprintf(reply, "Error, min %d and max %d", FLOOD_RETRY_COUNT_MIN, FLOOD_RETRY_COUNT_MAX);
        }
      } else if (memcmp(config, "flood.retry.path ", 17) == 0) {
        uint8_t path_gate;
        if (parseFloodRetryPathGate(&config[17], path_gate)) {
          _prefs->flood_retry_path_gate = path_gate;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, must be 0-63 or off");
        }
      } else if (memcmp(config, "flood.retry.prefixes ", 21) == 0) {
        if (parseFloodRetryPrefixes(_prefs, &config[21])) {
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, use comma-separated 3-byte hex prefixes");
        }
      } else if (memcmp(config, "flood.retry.ignore ", 19) == 0) {
        if (parseFloodRetryPrefixList(_prefs->flood_retry_ignore_prefixes,
                                      FLOOD_RETRY_IGNORE_PREFIXES, &config[19])) {
          savePrefs();
          strcpy(reply, "OK");
        } else {
          sprintf(reply, "Error, use up to %u comma-separated 3-byte hex prefixes",
                  (unsigned int)FLOOD_RETRY_IGNORE_PREFIXES);
        }
      } else if (memcmp(config, "flood.retry.bridge ", 19) == 0) {
        if (memcmp(&config[19], "on", 2) == 0) {
          _prefs->flood_retry_bridge_enabled = 1;
          savePrefs();
          strcpy(reply, "OK");
        } else if (memcmp(&config[19], "off", 3) == 0) {
          _prefs->flood_retry_bridge_enabled = 0;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, must be on or off");
        }
      } else if (memcmp(config, "flood.retry.bucket ", 19) == 0) {
        const char* params = &config[19];
        uint8_t bucket = atoi(params);
        const char* list = strchr(params, ' ');
        if (bucket < 1 || bucket > FLOOD_RETRY_BRIDGE_BUCKETS || list == NULL || *(list + 1) == 0) {
          sprintf(reply, "Error, usage: set flood.retry.bucket <1-%d> <prefixes|none>", FLOOD_RETRY_BRIDGE_BUCKETS);
        } else if (parseFloodRetryPrefixList(_prefs->flood_retry_bridge_buckets[bucket - 1],
                                             FLOOD_RETRY_BUCKET_PREFIXES, list + 1)) {
          savePrefs();
          strcpy(reply, "OK");
        } else {
          sprintf(reply, "Error, use up to %u comma-separated 3-byte hex prefixes",
                  (unsigned int)FLOOD_RETRY_BUCKET_PREFIXES);
        }
      } else if (memcmp(config, "direct.txdelay ", 15) == 0) {
        float f = atof(&config[15]);
        if (f >= 0) {
          _prefs->direct_tx_delay_factor = f;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, "direct.retry.heard ", 19) == 0) {
        if (memcmp(&config[19], "on", 2) == 0) {
          _prefs->direct_retry_recent_enabled = 1;
          savePrefs();
          strcpy(reply, "OK");
        } else if (memcmp(&config[19], "off", 3) == 0) {
          _prefs->direct_retry_recent_enabled = 0;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, must be on or off");
        }
      } else if (memcmp(config, "direct.retry.margin ", 20) == 0) {
        float db = atof(&config[20]);
        if (db >= 0 && db <= DIRECT_RETRY_SNR_MARGIN_DB_MAX) {
          _prefs->direct_retry_snr_margin_db = directRetryMarginDbToX4(db);
          savePrefs();
          strcpy(reply, "OK");
        } else {
          sprintf(reply, "Error, min 0 and max %d", DIRECT_RETRY_SNR_MARGIN_DB_MAX);
        }
      } else if (memcmp(config, "direct.retry.preset ", 20) == 0) {
        uint8_t preset;
        if (parseDirectRetryPreset(&config[20], preset)) {
          applyDirectRetryPreset(_prefs, preset);
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, must be infra, rooftop, mobile, 0, 1, or 2");
        }
      } else if (memcmp(config, "direct.retry.count ", 19) == 0) {
        int count = atoi(&config[19]);
        if (count >= DIRECT_RETRY_COUNT_MIN && count <= DIRECT_RETRY_COUNT_MAX) {
          _prefs->direct_retry_attempts = (uint8_t)count;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          sprintf(reply, "Error, min %d and max %d", DIRECT_RETRY_COUNT_MIN, DIRECT_RETRY_COUNT_MAX);
        }
      } else if (memcmp(config, "direct.retry.base ", 18) == 0) {
        int delay_ms = atoi(&config[18]);
        if (delay_ms >= DIRECT_RETRY_BASE_MS_MIN && delay_ms <= DIRECT_RETRY_BASE_MS_MAX) {
          _prefs->direct_retry_base_ms = (uint16_t)delay_ms;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          sprintf(reply, "Error, min %d and max %d", DIRECT_RETRY_BASE_MS_MIN, DIRECT_RETRY_BASE_MS_MAX);
        }
      } else if (memcmp(config, "direct.retry.step ", 18) == 0) {
        int delay_ms = atoi(&config[18]);
        if (delay_ms >= DIRECT_RETRY_STEP_MS_MIN && delay_ms <= DIRECT_RETRY_STEP_MS_MAX) {
          _prefs->direct_retry_step_ms = (uint16_t)delay_ms;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          sprintf(reply, "Error, min %d and max %d", DIRECT_RETRY_STEP_MS_MIN, DIRECT_RETRY_STEP_MS_MAX);
        }
      } else if (memcmp(config, "owner.info ", 11) == 0) {
        config += 11;
        char *dp = _prefs->owner_info;
        while (*config && dp - _prefs->owner_info < sizeof(_prefs->owner_info)-1) {
          *dp++ = (*config == '|') ? '\n' : *config;    // translate '|' to newline chars
          config++;
        }
        *dp = 0;
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "path.hash.mode ", 15) == 0) {
        config += 15;
        uint8_t mode = atoi(config);
        if (mode < 3) {
          _prefs->path_hash_mode = mode;
          savePrefs();
          strcpy(reply, "OK");
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
          savePrefs();
          strcpy(reply, "OK");
        }
      } else if (memcmp(config, "tx ", 3) == 0) {
        _prefs->tx_power_dbm = atoi(&config[3]);
        savePrefs();
        _callbacks->setTxPower(_prefs->tx_power_dbm);
        strcpy(reply, "OK");
      } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
        _prefs->freq = atof(&config[5]);
        savePrefs();
        strcpy(reply, "OK - reboot to apply");
#ifdef WITH_BRIDGE
      } else if (memcmp(config, "bridge.enabled ", 15) == 0) {
        _prefs->bridge_enabled = memcmp(&config[15], "on", 2) == 0;
        _callbacks->setBridgeState(_prefs->bridge_enabled);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "bridge.delay ", 13) == 0) {
        int delay = _atoi(&config[13]);
        if (delay >= 0 && delay <= 10000) {
          _prefs->bridge_delay = (uint16_t)delay;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error: delay must be between 0-10000 ms");
        }
      } else if (memcmp(config, "bridge.source ", 14) == 0) {
        _prefs->bridge_pkt_src = memcmp(&config[14], "rx", 2) == 0;
        savePrefs();
        strcpy(reply, "OK");
#endif
#ifdef WITH_RS232_BRIDGE
      } else if (memcmp(config, "bridge.baud ", 12) == 0) {
        uint32_t baud = atoi(&config[12]);
        if (baud >= 9600 && baud <= BRIDGE_MAX_BAUD) {
          _prefs->bridge_baud = (uint32_t)baud;
          _callbacks->restartBridge();
          savePrefs();
          strcpy(reply, "OK");
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
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error: channel must be between 1-14");
        }
      } else if (memcmp(config, "bridge.secret ", 14) == 0) {
        StrHelper::strncpy(_prefs->bridge_secret, &config[14], sizeof(_prefs->bridge_secret));
        _callbacks->restartBridge();
        savePrefs();
        strcpy(reply, "OK");
#endif
      } else if (memcmp(config, "adc.multiplier ", 15) == 0) {
        _prefs->adc_multiplier = atof(&config[15]);
        if (_board->setAdcMultiplier(_prefs->adc_multiplier)) {
          savePrefs();
          if (_prefs->adc_multiplier == 0.0f) {
            strcpy(reply, "OK - using default board multiplier");
          } else {
            sprintf(reply, "OK - multiplier set to %.3f", _prefs->adc_multiplier);
          }
        } else {
          _prefs->adc_multiplier = 0.0f;
          strcpy(reply, "Error: unsupported by this board");
        };
      } else {
        sprintf(reply, "unknown config: %s", config);
      }
    } else if (sender_timestamp == 0 && strcmp(command, "erase") == 0) {
      bool s = _callbacks->formatFileSystem();
      sprintf(reply, "File system erase: %s", s ? "OK" : "Err");
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
      strcpy(reply, "On - Immediate effect");
#elif defined(ESP32) && !defined(WITH_BRIDGE)
      _prefs->powersaving_enabled = 1;
      savePrefs();
      strcpy(reply, "On - After 2 minutes");
#elif defined(WITH_BRIDGE)
      strcpy(reply, "Bridge not supported");
#else
      strcpy(reply, "Board not supported");
#endif
    } else if (memcmp(command, "powersaving off", 15) == 0) {
      _prefs->powersaving_enabled = 0;
      savePrefs();
      strcpy(reply, "Off");
    } else if (memcmp(command, "powersaving", 11) == 0) {
      if (_prefs->powersaving_enabled) {
        strcpy(reply, "On");
      } else {
        strcpy(reply, "Off");
      }
    } else if (memcmp(command, "sensor", 6) == 0) {
      // I2C
#if defined(ENV_PIN_SDA) && defined(ENV_PIN_SCL)
      sprintf(reply, "I2C Wire1: SDA=%s,SCL=%s\r\n", STR(ENV_PIN_SDA), STR(ENV_PIN_SCL));
#elif defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      sprintf(reply, "I2C Wire: SDA=%s, SCL=%s\r\n", STR(PIN_BOARD_SDA), STR(PIN_BOARD_SCL));
#elif defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
      sprintf(reply, "I2C Wire: SDA=%s, SCL=%s\r\n", STR(PIN_WIRE_SDA), STR(PIN_WIRE_SCL));
#else
      sprintf(reply, "I2C GPIOs not defined\r\n");
#endif

      // GPS
#if defined(PIN_GPS_RX) && defined(PIN_GPS_TX)
      sprintf(reply + strlen(reply), "GPS Serial: RX=%s, TX=%s", STR(PIN_GPS_RX), STR(PIN_GPS_TX));
#ifdef ENV_INCLUDE_GPS> 0
      sprintf(reply + strlen(reply), ". Configured");
#else
      sprintf(reply + strlen(reply), ". Not configured");
#endif
#else
      sprintf(reply + strlen(reply), "GPS Serial not defined");
#endif
    } else if (memcmp(command, "powerlog", 8) == 0) {
      sprintf(reply, "Last reset reason: %s", _board->getResetReasonString(_board->getResetReason()));
#if defined(NRF52_PLATFORM)
      sprintf(reply + strlen(reply), "\r\nLast shutdown reason: %s",
              _board->getShutdownReasonString(_board->getShutdownReason()));
      sprintf(reply + strlen(reply), "\r\nLast boot voltage: %u mV", _board->getBootVoltage());
#endif
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
      savePrefs();
      float actual = 100.0f / (_prefs->airtime_factor + 1.0f);
      int a_int = (int)actual;
      int a_frac = (int)((actual - a_int) * 10.0f + 0.5f);
      sprintf(reply, "OK - %d.%d%%", a_int, a_frac);
    }
  } else if (memcmp(config, "af ", 3) == 0) {
    _prefs->airtime_factor = atof(&config[3]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "int.thresh ", 11) == 0) {
    _prefs->interference_threshold = atoi(&config[11]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "agc.reset.interval ", 19) == 0) {
    _prefs->agc_reset_interval = atoi(&config[19]) / 4;
    savePrefs();
    sprintf(reply, "OK - interval rounded to %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
  } else if (memcmp(config, "multi.acks ", 11) == 0) {
    _prefs->multi_acks = atoi(&config[11]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "allow.read.only ", 16) == 0) {
    _prefs->allow_read_only = memcmp(&config[16], "on", 2) == 0;
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "flood.advert.interval ", 22) == 0) {
    int hours = _atoi(&config[22]);
    if ((hours > 0 && hours < 3) || (hours > 168)) {
      strcpy(reply, "Error: interval range is 3-168 hours");
    } else {
      _prefs->flood_advert_interval = (uint8_t)(hours);
      _callbacks->updateFloodAdvertTimer();
      savePrefs();
      strcpy(reply, "OK");
    }
  } else if (memcmp(config, "advert.interval ", 16) == 0) {
    int mins = _atoi(&config[16]);
    if ((mins > 0 && mins < MIN_LOCAL_ADVERT_INTERVAL) || (mins > 240)) {
      sprintf(reply, "Error: interval range is %d-240 minutes", MIN_LOCAL_ADVERT_INTERVAL);
    } else {
      _prefs->advert_interval = (uint8_t)(mins / 2);
      _callbacks->updateAdvertTimer();
      savePrefs();
      strcpy(reply, "OK");
    }
  } else if (memcmp(config, "guest.password ", 15) == 0) {
    StrHelper::strncpy(_prefs->guest_password, &config[15], sizeof(_prefs->guest_password));
    savePrefs();
    strcpy(reply, "OK");
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
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, bad chars");
    }
  } else if (memcmp(config, "repeat ", 7) == 0) {
    _prefs->disable_fwd = memcmp(&config[7], "off", 3) == 0;
    savePrefs();
    strcpy(reply, _prefs->disable_fwd ? "OK - repeat is now OFF" : "OK - repeat is now ON");
#if defined(USE_SX1262) || defined(USE_SX1268)
  } else if (memcmp(config, "radio.rxgain ", 13) == 0) {
    _prefs->rx_boosted_gain = memcmp(&config[13], "on", 2) == 0;
    strcpy(reply, "OK");
    savePrefs();
    _callbacks->setRxBoostedGain(_prefs->rx_boosted_gain);
#endif
  } else if (memcmp(config, "radio.fem.rxgain ", 17) == 0) {
    if (!_board->canControlLoRaFemLna()) {
      strcpy(reply, "Error: unsupported");
    } else if (memcmp(&config[17], "on", 2) == 0) {
      if (_board->setLoRaFemLnaEnabled(true)) {
        _prefs->radio_fem_rxgain = 1;
        savePrefs();
        strcpy(reply, "OK - LoRa FEM RX gain on");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else if (memcmp(&config[17], "off", 3) == 0) {
      if (_board->setLoRaFemLnaEnabled(false)) {
        _prefs->radio_fem_rxgain = 0;
        savePrefs();
        strcpy(reply, "OK - LoRa FEM RX gain off");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
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
      _callbacks->savePrefs();
      strcpy(reply, "OK - reboot to apply");
    } else {
      strcpy(reply, "Error, invalid radio params");
    }
  } else if (memcmp(config, "lat ", 4) == 0) {
    _prefs->node_lat = atof(&config[4]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "lon ", 4) == 0) {
    _prefs->node_lon = atof(&config[4]);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "rxdelay ", 8) == 0) {
    float db = atof(&config[8]);
    if (db >= 0) {
      _prefs->rx_delay_base = db;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, cannot be negative");
    }
  } else if (memcmp(config, "txdelay ", 8) == 0) {
    float f = atof(&config[8]);
    if (f >= 0) {
      _prefs->tx_delay_factor = f;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, cannot be negative");
    }
  } else if (memcmp(config, "flood.max ", 10) == 0) {
    uint8_t m = atoi(&config[10]);
    if (m <= 64) {
      _prefs->flood_max = m;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, max 64");
    }
  } else if (memcmp(config, "flood.retry.preset ", 19) == 0) {
    uint8_t preset;
    if (parseDirectRetryPreset(&config[19], preset)) {
      applyFloodRetryPreset(_prefs, preset);
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be infra, rooftop, mobile, 0, 1, or 2");
    }
  } else if (memcmp(config, "flood.retry.count ", 18) == 0) {
    uint8_t count;
    if (parseUint8Strict(&config[18], FLOOD_RETRY_COUNT_MIN, FLOOD_RETRY_COUNT_MAX, count)) {
      _prefs->flood_retry_attempts = count;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      sprintf(reply, "Error, min %d and max %d", FLOOD_RETRY_COUNT_MIN, FLOOD_RETRY_COUNT_MAX);
    }
  } else if (memcmp(config, "flood.retry.path ", 17) == 0) {
    uint8_t path_gate;
    if (parseFloodRetryPathGate(&config[17], path_gate)) {
      _prefs->flood_retry_path_gate = path_gate;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-63 or off");
    }
  } else if (memcmp(config, "flood.retry.prefixes ", 21) == 0) {
    if (parseFloodRetryPrefixes(_prefs, &config[21])) {
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, use comma-separated 3-byte hex prefixes");
    }
  } else if (memcmp(config, "flood.retry.ignore ", 19) == 0) {
    if (parseFloodRetryPrefixList(_prefs->flood_retry_ignore_prefixes,
                                  FLOOD_RETRY_IGNORE_PREFIXES, &config[19])) {
      savePrefs();
      strcpy(reply, "OK");
    } else {
      sprintf(reply, "Error, use up to %u comma-separated 3-byte hex prefixes",
              (unsigned int)FLOOD_RETRY_IGNORE_PREFIXES);
    }
  } else if (memcmp(config, "flood.retry.bridge ", 19) == 0) {
    if (memcmp(&config[19], "on", 2) == 0) {
      _prefs->flood_retry_bridge_enabled = 1;
      savePrefs();
      strcpy(reply, "OK");
    } else if (memcmp(&config[19], "off", 3) == 0) {
      _prefs->flood_retry_bridge_enabled = 0;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be on or off");
    }
  } else if (memcmp(config, "flood.retry.bucket ", 19) == 0) {
    const char* params = &config[19];
    uint8_t bucket = atoi(params);
    const char* list = strchr(params, ' ');
    if (bucket < 1 || bucket > FLOOD_RETRY_BRIDGE_BUCKETS || list == NULL || *(list + 1) == 0) {
      sprintf(reply, "Error, usage: set flood.retry.bucket <1-%d> <prefixes|none>", FLOOD_RETRY_BRIDGE_BUCKETS);
    } else if (parseFloodRetryPrefixList(_prefs->flood_retry_bridge_buckets[bucket - 1],
                                         FLOOD_RETRY_BUCKET_PREFIXES, list + 1)) {
      savePrefs();
      strcpy(reply, "OK");
    } else {
      sprintf(reply, "Error, use up to %u comma-separated 3-byte hex prefixes",
              (unsigned int)FLOOD_RETRY_BUCKET_PREFIXES);
    }
  } else if (memcmp(config, "direct.txdelay ", 15) == 0) {
    float f = atof(&config[15]);
    if (f >= 0) {
      _prefs->direct_tx_delay_factor = f;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, cannot be negative");
    }
  } else if (memcmp(config, "direct.retry.heard ", 19) == 0) {
    if (memcmp(&config[19], "on", 2) == 0) {
      _prefs->direct_retry_recent_enabled = 1;
      savePrefs();
      strcpy(reply, "OK");
    } else if (memcmp(&config[19], "off", 3) == 0) {
      _prefs->direct_retry_recent_enabled = 0;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be on or off");
    }
  } else if (memcmp(config, "direct.retry.margin ", 20) == 0) {
    float db = atof(&config[20]);
    if (db >= 0 && db <= DIRECT_RETRY_SNR_MARGIN_DB_MAX) {
      _prefs->direct_retry_snr_margin_db = directRetryMarginDbToX4(db);
      savePrefs();
      strcpy(reply, "OK");
    } else {
      sprintf(reply, "Error, min 0 and max %d", DIRECT_RETRY_SNR_MARGIN_DB_MAX);
    }
  } else if (memcmp(config, "direct.retry.preset ", 20) == 0) {
    uint8_t preset;
    if (parseDirectRetryPreset(&config[20], preset)) {
      applyDirectRetryPreset(_prefs, preset);
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be infra, rooftop, mobile, 0, 1, or 2");
    }
  } else if (memcmp(config, "direct.retry.count ", 19) == 0) {
    int count = atoi(&config[19]);
    if (count >= DIRECT_RETRY_COUNT_MIN && count <= DIRECT_RETRY_COUNT_MAX) {
      _prefs->direct_retry_attempts = (uint8_t)count;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      sprintf(reply, "Error, min %d and max %d", DIRECT_RETRY_COUNT_MIN, DIRECT_RETRY_COUNT_MAX);
    }
  } else if (memcmp(config, "direct.retry.base ", 18) == 0) {
    int delay_ms = atoi(&config[18]);
    if (delay_ms >= DIRECT_RETRY_BASE_MS_MIN && delay_ms <= DIRECT_RETRY_BASE_MS_MAX) {
      _prefs->direct_retry_base_ms = (uint16_t)delay_ms;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      sprintf(reply, "Error, min %d and max %d", DIRECT_RETRY_BASE_MS_MIN, DIRECT_RETRY_BASE_MS_MAX);
    }
  } else if (memcmp(config, "direct.retry.step ", 18) == 0) {
    int delay_ms = atoi(&config[18]);
    if (delay_ms >= DIRECT_RETRY_STEP_MS_MIN && delay_ms <= DIRECT_RETRY_STEP_MS_MAX) {
      _prefs->direct_retry_step_ms = (uint16_t)delay_ms;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      sprintf(reply, "Error, min %d and max %d", DIRECT_RETRY_STEP_MS_MIN, DIRECT_RETRY_STEP_MS_MAX);
    }
  } else if (memcmp(config, "owner.info ", 11) == 0) {
    config += 11;
    char *dp = _prefs->owner_info;
    while (*config && dp - _prefs->owner_info < sizeof(_prefs->owner_info)-1) {
      *dp++ = (*config == '|') ? '\n' : *config;    // translate '|' to newline chars
      config++;
    }
    *dp = 0;
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "path.hash.mode ", 15) == 0) {
    config += 15;
    uint8_t mode = atoi(config);
    if (mode < 3) {
      _prefs->path_hash_mode = mode;
      savePrefs();
      strcpy(reply, "OK");
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
      savePrefs();
      strcpy(reply, "OK");
    }
  } else if (memcmp(config, "tx ", 3) == 0) {
    _prefs->tx_power_dbm = atoi(&config[3]);
    savePrefs();
    _callbacks->setTxPower(_prefs->tx_power_dbm);
    strcpy(reply, "OK");
  } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
    _prefs->freq = atof(&config[5]);
    savePrefs();
    strcpy(reply, "OK - reboot to apply");
#ifdef WITH_BRIDGE
  } else if (memcmp(config, "bridge.enabled ", 15) == 0) {
    _prefs->bridge_enabled = memcmp(&config[15], "on", 2) == 0;
    _callbacks->setBridgeState(_prefs->bridge_enabled);
    savePrefs();
    strcpy(reply, "OK");
  } else if (memcmp(config, "bridge.delay ", 13) == 0) {
    int delay = _atoi(&config[13]);
    if (delay >= 0 && delay <= 10000) {
      _prefs->bridge_delay = (uint16_t)delay;
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error: delay must be between 0-10000 ms");
    }
  } else if (memcmp(config, "bridge.source ", 14) == 0) {
    _prefs->bridge_pkt_src = memcmp(&config[14], "rx", 2) == 0;
    savePrefs();
    strcpy(reply, "OK");
#endif
#ifdef WITH_RS232_BRIDGE
  } else if (memcmp(config, "bridge.baud ", 12) == 0) {
    uint32_t baud = atoi(&config[12]);
    if (baud >= 9600 && baud <= BRIDGE_MAX_BAUD) {
      _prefs->bridge_baud = (uint32_t)baud;
      _callbacks->restartBridge();
      savePrefs();
      strcpy(reply, "OK");
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
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error: channel must be between 1-14");
    }
  } else if (memcmp(config, "bridge.secret ", 14) == 0) {
    StrHelper::strncpy(_prefs->bridge_secret, &config[14], sizeof(_prefs->bridge_secret));
    _callbacks->restartBridge();
    savePrefs();
    strcpy(reply, "OK");
#endif
  } else if (memcmp(config, "adc.multiplier ", 15) == 0) {
    _prefs->adc_multiplier = atof(&config[15]);
    if (_board->setAdcMultiplier(_prefs->adc_multiplier)) {
      savePrefs();
      if (_prefs->adc_multiplier == 0.0f) {
        strcpy(reply, "OK - using default board multiplier");
      } else {
        sprintf(reply, "OK - multiplier set to %.3f", _prefs->adc_multiplier);
      }
    } else {
      _prefs->adc_multiplier = 0.0f;
      strcpy(reply, "Error: unsupported");
    };
  } else {
    strcpy(reply, "unknown config: ");
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
#if defined(USE_SX1262) || defined(USE_SX1268)
  } else if (memcmp(config, "radio.rxgain", 12) == 0) {
    sprintf(reply, "> %s", _prefs->rx_boosted_gain ? "on" : "off");
#endif
  } else if (memcmp(config, "radio.fem.rxgain", 16) == 0) {
    if (!_board->canControlLoRaFemLna()) {
      strcpy(reply, "Error: unsupported");
    } else {
      sprintf(reply, "> %s", _board->isLoRaFemLnaEnabled() ? "on" : "off");
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
  } else if (memcmp(config, "flood.max", 9) == 0) {
    sprintf(reply, "> %d", (uint32_t)_prefs->flood_max);
  } else if (memcmp(config, "flood.retry.count", 17) == 0) {
    sprintf(reply, "> %d", (uint32_t)floodRetryEffectiveCount(_prefs));
  } else if (memcmp(config, "flood.retry.path", 16) == 0) {
    char path_gate[8];
    formatFloodRetryPathGate(path_gate, _prefs->flood_retry_path_gate);
    sprintf(reply, "> %s", path_gate);
  } else if (memcmp(config, "flood.retry.prefixes", 20) == 0) {
    formatFloodRetryPrefixes(tmp, _prefs);
    sprintf(reply, "> %s", tmp[0] ? tmp : "none");
  } else if (memcmp(config, "flood.retry.ignore", 18) == 0) {
    formatFloodRetryPrefixList(tmp, _prefs->flood_retry_ignore_prefixes, FLOOD_RETRY_IGNORE_PREFIXES);
    sprintf(reply, "> %s", tmp[0] ? tmp : "none");
  } else if (memcmp(config, "flood.retry.bridge", 18) == 0) {
    sprintf(reply, "> %s", _prefs->flood_retry_bridge_enabled ? "on" : "off");
  } else if (memcmp(config, "flood.retry.bucket.", 19) == 0) {
    uint8_t bucket = atoi(&config[19]);
    if (bucket >= 1 && bucket <= FLOOD_RETRY_BRIDGE_BUCKETS) {
      formatFloodRetryBridgeBucket(tmp, _prefs, bucket - 1);
      sprintf(reply, "> %s", tmp[0] ? tmp : "none");
    } else {
      sprintf(reply, "Error, bucket 1-%d", FLOOD_RETRY_BRIDGE_BUCKETS);
    }
  } else if (memcmp(config, "flood.retry.preset", 18) == 0) {
    uint8_t preset = floodRetryPresetForPrefs(_prefs);
    sprintf(reply, "> %d,%s",
            (uint32_t)preset,
            directRetryPresetName(preset));
  } else if (memcmp(config, "direct.txdelay", 14) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(_prefs->direct_tx_delay_factor));
  } else if (memcmp(config, "direct.retry.heard", 18) == 0) {
    sprintf(reply, "> %s", _prefs->direct_retry_recent_enabled ? "on" : "off");
  } else if (memcmp(config, "direct.retry.margin", 19) == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(directRetryMarginX4ToDb(directRetryEffectiveMarginX4(_prefs))));
  } else if (memcmp(config, "direct.retry.preset", 19) == 0) {
    sprintf(reply, "> %d,%s",
            (uint32_t)directRetryPresetOrDefault(_prefs->direct_retry_preset),
            directRetryPresetName(_prefs->direct_retry_preset));
  } else if (memcmp(config, "direct.retry.count", 18) == 0) {
    sprintf(reply, "> %d", (uint32_t)directRetryEffectiveCount(_prefs));
  } else if (memcmp(config, "direct.retry.base", 17) == 0) {
    sprintf(reply, "> %d", (uint32_t)directRetryEffectiveBaseMs(_prefs));
  } else if (memcmp(config, "direct.retry.step", 17) == 0) {
    sprintf(reply, "> %d", (uint32_t)directRetryEffectiveStepMs(_prefs));
  } else if (memcmp(config, "owner.info", 10) == 0) {
    *reply++ = '>';
    *reply++ = ' ';
    const char* sp = _prefs->owner_info;
    while (*sp) {
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
#ifdef NRF52_POWER_MANAGEMENT
    sprintf(reply, "> Reset: %s; Shutdown: %s",
      _board->getResetReasonString(_board->getResetReason()),
      _board->getShutdownReasonString(_board->getShutdownReason()));
#else
    strcpy(reply, "ERROR: Power management not supported");
#endif
  } else if (memcmp(config, "pwrmgt.bootmv", 13) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
    sprintf(reply, "> %u mV", _board->getBootVoltage());
#else
    strcpy(reply, "ERROR: Power management not supported");
#endif
  } else {
    sprintf(reply, "??: %s", config);
  }
}

void CommonCLI::handleRegionCmd(char* command, char* reply) {
  reply[0] = 0;

  const char* parts[4];
  int n = mesh::Utils::parseTextParts(command, parts, 4, ' ');
  if (n == 1) {
    _region_map->exportTo(reply, 160);
  } else if (n >= 2 && strcmp(parts[1], "load") == 0) {
    _callbacks->startRegionsLoad();
  } else if (n >= 2 && strcmp(parts[1], "save") == 0) {
    _prefs->discovery_mod_timestamp = getRTCClock()->getCurrentTime();   // this node is now 'modified' (for discovery info)
    savePrefs();
    bool success = _callbacks->saveRegions();
    strcpy(reply, success ? "OK" : "Err - save failed");
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
