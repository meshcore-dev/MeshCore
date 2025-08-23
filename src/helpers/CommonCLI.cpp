#include <Arduino.h>
#include "CommonCLI.h"
#include "TxtDataHelpers.h"
#include <RTClib.h>

// ---------- Command and key registry for zero-maintenance `help` ----------
// Define command strings once, use everywhere, and list them for help output.
static constexpr const char* CMD_REBOOT        = "reboot";
static constexpr const char* CMD_ADVERT        = "advert";
static constexpr const char* CMD_CLOCK         = "clock";
static constexpr const char* CMD_CLOCK_SYNC    = "clock sync";
static constexpr const char* CMD_START_OTA     = "start ota";
static constexpr const char* CMD_NEIGHBORS     = "neighbors";
static constexpr const char* CMD_TEMPRADIO     = "tempradio";
static constexpr const char* CMD_PASSWORD      = "password";
static constexpr const char* CMD_CLEAR_STATS   = "clear stats";
static constexpr const char* CMD_GET           = "get";
static constexpr const char* CMD_SET           = "set";
static constexpr const char* CMD_ERASE         = "erase";
static constexpr const char* CMD_VER           = "ver";
static constexpr const char* CMD_LOG_START     = "log start";
static constexpr const char* CMD_LOG_STOP      = "log stop";
static constexpr const char* CMD_LOG_ERASE     = "log erase";
static constexpr const char* CMD_LOG_DUMP      = "log";
static constexpr const char* CMD_HELP          = "help";

// Config keys supported by get/set. Listed once for help, used in comparisons.
static constexpr const char* KEY_AF                    = "af";
static constexpr const char* KEY_INT_THRESH            = "int.thresh";
static constexpr const char* KEY_AGC_RESET_INTERVAL    = "agc.reset.interval";
static constexpr const char* KEY_MULTI_ACKS            = "multi.acks";
static constexpr const char* KEY_ALLOW_READ_ONLY       = "allow.read.only";
static constexpr const char* KEY_FLOOD_ADV_INT         = "flood.advert.interval";
static constexpr const char* KEY_ADVERT_INTERVAL       = "advert.interval";
static constexpr const char* KEY_GUEST_PASSWORD        = "guest.password";
static constexpr const char* KEY_NAME                  = "name";
static constexpr const char* KEY_REPEAT                = "repeat";
static constexpr const char* KEY_LAT                   = "lat";
static constexpr const char* KEY_LON                   = "lon";
static constexpr const char* KEY_RADIO                 = "radio"; 
static constexpr const char* KEY_RXDELAY               = "rxdelay";
static constexpr const char* KEY_TXDELAY               = "txdelay";
static constexpr const char* KEY_DIRECT_TXDELAY        = "direct.txdelay";
static constexpr const char* KEY_FLOOD_MAX             = "flood.max";
static constexpr const char* KEY_TX                    = "tx";
static constexpr const char* KEY_FREQ                  = "freq";
static constexpr const char* KEY_PUBLIC_KEY            = "public.key";
static constexpr const char* KEY_ROLE                  = "role";

// Help messages are small, so cannot include parameter detail
// which is why this is being split into sub-sections

static constexpr const char* HELP_DEVICE_CMDS[] = {
  CMD_REBOOT, CMD_CLOCK, CMD_CLOCK_SYNC, CMD_START_OTA, CMD_ERASE
}
static constexpr const char* HELP_DEVICE_KEYS[] = {
  KEY_NAME, KEY_LAT, KEY_LON, KEY_ROLE
}

static constexpr const char* HELP_RADIO_CMDS[] = {
  CMD_TEMPRADIO
}
static constexpr const char* HELP_RADIO_KEYS_STANDARD[] = {
  KEY_RADIO, KEY_TX, KEY_FREQ
}
static constexpr const char* HELP_RADIO_KEYS_ADVANCED[] = {
  KEY_AF, KEY_INT_THRESH, KEY_AGC_RESET_INTERVAL, KEY_RXDELAY, KEY_TXDELAY, KEY_DIRECT_TXDELAY
}

static constexpr const char* HELP_MESH_CMDS[] = {
  CMD_ADVERT
}
static constexpr const char* HELP_MESH_KEYS[] = {
  KEY_MULTI_ACKS, KEY_FLOOD_ADV_INT, KEY_ADVERT_INTERVAL, KEY_REPEAT, KEY_ALLOW_READ_ONLY, KEY_FLOOD_MAX
}

static constexpr const char* HELP_AUTH_CMDS[] = {
  CMD_PASSWORD
}
static constexpr const char* HELP_AUTH_KEYS[] = {
  KEY_GUEST_PASSWORD, KEY_PUBLIC_KEY
}

static constexpr const char* HELP_DEBUG_CMDS[] = {
  CMD_CLEAR_STATS, CMD_VER, CMD_LOG_START, CMD_LOG_STOP, CMD_LOG_ERASE, CMD_LOG_DUMP, CMD_NEIGHBORS
}


// ---- Split help formatters to keep replies short ----
static void format_help_overview(char* reply) {
  // Tiny overview that always fits typical reply buffers
  // Themes are: device, radio, mesh, auth, debug
  sprintf(reply, "help <theme> [cmds|keys].\n Theme can be: device, radio, mesh, auth, debug\n");
}

// Theme: device
static void format_help_device_cmds(char* reply) {
  char* p = reply;
  p += sprintf(p, "Commands: ");
  for (size_t i = 0; i < (sizeof(HELP_DEVICE_CMDS) / sizeof(HELP_DEVICE_CMDS[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_DEVICE_CMDS[i], "\n");
  }
}
static void format_help_device_keys(char* reply) {
  char* p = reply;
  p += sprintf(p, "Keys (for get /set): ");
  for (size_t i = 0; i < (sizeof(HELP_DEVICE_KEYS) / sizeof(HELP_DEVICE_KEYS[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_DEVICE_KEYS[i], "\n");
  }
}

// Theme: radio
static void format_help_radio_cmds(char* reply) {
  char* p = reply;
  p += sprintf(p, "Help for radio is in 3 sections: radio_cmds, radio_keys_standard, radio_keys_advanced.");
  for (size_t i = 0; i < (sizeof(HELP_RADIO_CMDS) / sizeof(HELP_RADIO_CMDS[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_RADIO_CMDS[i], "\n");
  }
}
static void format_help_radio_standard_keys(char* reply) {
  char* p = reply;
  p += sprintf(p, "Keys (for get /set): ");
  for (size_t i = 0; i < (sizeof(HELP_RADIO_KEYS_STANDARD) / sizeof(HELP_RADIO_KEYS_STANDARD[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_RADIO_KEYS_STANDARD[i], "\n");
  }
}
static void format_help_radio_advanced_keys(char* reply) {
  char* p = reply;
  p += sprintf(p, "Keys (for get /set): ");
  for (size_t i = 0; i < (sizeof(HELP_RADIO_KEYS_ADVANCED) / sizeof(HELP_RADIO_KEYS_ADVANCED[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_RADIO_KEYS_ADVANCED[i], "\n");
  }
}

// Theme: mesh
static void format_help_mesh_cmds(char* reply) {
  char* p = reply;
  p += sprintf(p, "Commands: ");
  for (size_t i = 0; i < (sizeof(HELP_MESH_CMDS) / sizeof(HELP_MESH_CMDS[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_MESH_CMDS[i], "\n");
  }
}
static void format_help_mesh_keys(char* reply) {
  char* p = reply;
  p += sprintf(p, "Keys (for get /set): ");
  for (size_t i = 0; i < (sizeof(HELP_MESH_KEYS) / sizeof(HELP_MESH_KEYS[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_MESH_KEYS[i], "\n");
  }
}

// Theme: auth
static void format_help_auth_cmds(char* reply) {
  char* p = reply;
  p += sprintf(p, "Commands: ");
  for (size_t i = 0; i < (sizeof(HELP_AUTH_CMDS) / sizeof(HELP_AUTH_CMDS[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_AUTH_CMDS[i], "\n");
  }
}
static void format_help_auth_keys(char* reply) {
  char* p = reply;
  p += sprintf(p, "Keys (for get /set): ");
  for (size_t i = 0; i < (sizeof(HELP_AUTH_KEYS) / sizeof(HELP_AUTH_KEYS[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_AUTH_KEYS[i], "\n");
  }
}

// Theme: debug
static void format_help_debug_cmds(char* reply) {
  char* p = reply;
  p += sprintf(p, "Commands: ");
  for (size_t i = 0; i < (sizeof(HELP_DEBUG_CMDS) / sizeof(HELP_DEBUG_CMDS[0])); ++i) {
    p += sprintf(p, "%s%s", HELP_DEBUG_CMDS[i], "\n");
  }
}

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
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

    file.read((uint8_t *) &_prefs->airtime_factor, sizeof(_prefs->airtime_factor));  // 0
    file.read((uint8_t *) &_prefs->node_name, sizeof(_prefs->node_name));  // 4
    file.read(pad, 4);   // 36
    file.read((uint8_t *) &_prefs->node_lat, sizeof(_prefs->node_lat));  // 40
    file.read((uint8_t *) &_prefs->node_lon, sizeof(_prefs->node_lon));  // 48
    file.read((uint8_t *) &_prefs->password[0], sizeof(_prefs->password));  // 56
    file.read((uint8_t *) &_prefs->freq, sizeof(_prefs->freq));   // 72
    file.read((uint8_t *) &_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));  // 76
    file.read((uint8_t *) &_prefs->disable_fwd, sizeof(_prefs->disable_fwd));  // 77
    file.read((uint8_t *) &_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.read((uint8_t *) pad, 1);  // 79  was 'unused'
    file.read((uint8_t *) &_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));  // 80
    file.read((uint8_t *) &_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.read((uint8_t *) &_prefs->guest_password[0], sizeof(_prefs->guest_password));  // 88
    file.read((uint8_t *) &_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor));  // 104
    file.read(pad, 4);   // 108
    file.read((uint8_t *) &_prefs->sf, sizeof(_prefs->sf));  // 112
    file.read((uint8_t *) &_prefs->cr, sizeof(_prefs->cr));  // 113
    file.read((uint8_t *) &_prefs->allow_read_only, sizeof(_prefs->allow_read_only));  // 114
    file.read((uint8_t *) &_prefs->multi_acks, sizeof(_prefs->multi_acks));  // 115
    file.read((uint8_t *) &_prefs->bw, sizeof(_prefs->bw));  // 116
    file.read((uint8_t *) &_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));  // 120
    file.read(pad, 3);   // 121
    file.read((uint8_t *) &_prefs->flood_max, sizeof(_prefs->flood_max));   // 124
    file.read((uint8_t *) &_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));  // 125
    file.read((uint8_t *) &_prefs->interference_threshold, sizeof(_prefs->interference_threshold));  // 126

    // sanitise bad pref values
    _prefs->rx_delay_base = constrain(_prefs->rx_delay_base, 0, 20.0f);
    _prefs->tx_delay_factor = constrain(_prefs->tx_delay_factor, 0, 2.0f);
    _prefs->direct_tx_delay_factor = constrain(_prefs->direct_tx_delay_factor, 0, 2.0f);
    _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 9.0f);
    _prefs->freq = constrain(_prefs->freq, 400.0f, 2500.0f);
    _prefs->bw = constrain(_prefs->bw, 62.5f, 500.0f);
    _prefs->sf = constrain(_prefs->sf, 7, 12);
    _prefs->cr = constrain(_prefs->cr, 5, 8);
    _prefs->tx_power_dbm = constrain(_prefs->tx_power_dbm, 1, 30);
    _prefs->multi_acks = constrain(_prefs->multi_acks, 0, 1);

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

    file.write((uint8_t *) &_prefs->airtime_factor, sizeof(_prefs->airtime_factor));  // 0
    file.write((uint8_t *) &_prefs->node_name, sizeof(_prefs->node_name));  // 4
    file.write(pad, 4);   // 36
    file.write((uint8_t *) &_prefs->node_lat, sizeof(_prefs->node_lat));  // 40
    file.write((uint8_t *) &_prefs->node_lon, sizeof(_prefs->node_lon));  // 48
    file.write((uint8_t *) &_prefs->password[0], sizeof(_prefs->password));  // 56
    file.write((uint8_t *) &_prefs->freq, sizeof(_prefs->freq));   // 72
    file.write((uint8_t *) &_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));  // 76
    file.write((uint8_t *) &_prefs->disable_fwd, sizeof(_prefs->disable_fwd));  // 77
    file.write((uint8_t *) &_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.write((uint8_t *) pad, 1);  // 79  was 'unused'
    file.write((uint8_t *) &_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));  // 80
    file.write((uint8_t *) &_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.write((uint8_t *) &_prefs->guest_password[0], sizeof(_prefs->guest_password));  // 88
    file.write((uint8_t *) &_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor));  // 104
    file.write(pad, 4);   // 108
    file.write((uint8_t *) &_prefs->sf, sizeof(_prefs->sf));  // 112
    file.write((uint8_t *) &_prefs->cr, sizeof(_prefs->cr));  // 113
    file.write((uint8_t *) &_prefs->allow_read_only, sizeof(_prefs->allow_read_only));  // 114
    file.write((uint8_t *) &_prefs->multi_acks, sizeof(_prefs->multi_acks));  // 115
    file.write((uint8_t *) &_prefs->bw, sizeof(_prefs->bw));  // 116
    file.write((uint8_t *) &_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));  // 120
    file.write(pad, 3);   // 121
    file.write((uint8_t *) &_prefs->flood_max, sizeof(_prefs->flood_max));   // 124
    file.write((uint8_t *) &_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));  // 125
    file.write((uint8_t *) &_prefs->interference_threshold, sizeof(_prefs->interference_threshold));  // 126

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

void CommonCLI::handleCommand(uint32_t sender_timestamp, const char* command, char* reply) {
    // Split help to keep messages short
    // device, radio, mesh, auth, debug
    if (memcmp(command, CMD_HELP, 4) == 0) {
      if (memcmp(command, "help device cmds", 16) == 0) { format_help_device_cmds(reply); return; }
      if (memcmp(command, "help device keys", 16) == 0) { format_help_device_keys(reply); return; }
      if (memcmp(command, "help radio cmds", 15)  == 0) { format_help_radio_cmds(reply);  return; }
      if (memcmp(command, "help radio std keys", 19)  == 0) { format_help_radio_standard_keys(reply);  return; }
      if (memcmp(command, "help radio adv keys", 19)  == 0) { format_help_radio_advanced_keys(reply);  return; }
      if (memcmp(command, "help mesh cmds", 14)  == 0) { format_help_mesh_cmds(reply);  return; }
      if (memcmp(command, "help mesh keys", 14)  == 0) { format_help_mesh_keys(reply);  return; }
      if (memcmp(command, "help auth cmds", 14)  == 0) { format_help_auth_cmds(reply);  return; }
      if (memcmp(command, "help auth keys", 14)  == 0) { format_help_auth_keys(reply);  return; }
      if (memcmp(command, "help debug", 10)  == 0) { format_help_debug_cmds(reply);  return; }
      format_help_overview(reply);
      return;
    }
    if (memcmp(command, CMD_REBOOT, 6) == 0) {
      _board->reboot();  // doesn't return
    } else if (memcmp(command, CMD_ADVERT, 6) == 0) {
      _callbacks->sendSelfAdvertisement(1500);  // longer delay, give CLI response time to be sent first
      strcpy(reply, "OK - Advert sent");
    } else if (memcmp(command, CMD_CLOCK_SYNC, 10) == 0) {
      uint32_t curr = getRTCClock()->getCurrentTime();
      if (sender_timestamp > curr) {
        getRTCClock()->setCurrentTime(sender_timestamp + 1);
        uint32_t now = getRTCClock()->getCurrentTime();
        DateTime dt = DateTime(now);
        sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
      } else {
        strcpy(reply, "ERR: clock cannot go backwards");
      }
    } else if (memcmp(command, CMD_START_OTA, 9) == 0) {
      if (!_board->startOTAUpdate(_prefs->node_name, reply)) {
        strcpy(reply, "Error");
      }
    } else if (memcmp(command, CMD_CLOCK, 5) == 0) {
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
    } else if (memcmp(command, CMD_NEIGHBORS, 9) == 0) {
      _callbacks->formatNeighborsReply(reply);
    } else if (memcmp(command, CMD_TEMPRADIO, 10) == 0) {
      strcpy(tmp, &command[10]);
      const char *parts[5];
      int num = mesh::Utils::parseTextParts(tmp, parts, 5);
      float freq  = num > 0 ? atof(parts[0]) : 0.0f;
      float bw    = num > 1 ? atof(parts[1]) : 0.0f;
      uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
      uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
      int temp_timeout_mins  = num > 4 ? atoi(parts[4]) : 0;
      if (freq >= 300.0f && freq <= 2500.0f && sf >= 7 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error, invalid params");
      }
    } else if (memcmp(command, CMD_PASSWORD, 9) == 0) {
      // change admin password
      StrHelper::strncpy(_prefs->password, &command[9], sizeof(_prefs->password));
      savePrefs();
      sprintf(reply, "password now: %s", _prefs->password);   // echo back just to let admin know for sure!!
    } else if (memcmp(command, CMD_CLEAR_STATS, 11) == 0) {
      _callbacks->clearStats();
      strcpy(reply, "(OK - stats reset)");
    } else if (memcmp(command, CMD_GET, 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, KEY_AF, 2) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->airtime_factor));
      } else if (memcmp(config, KEY_INT_THRESH, 10) == 0) {
        sprintf(reply, "> %d", (uint32_t) _prefs->interference_threshold);
      } else if (memcmp(config, KEY_AGC_RESET_INTERVAL, 18) == 0) {
        sprintf(reply, "> %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
      } else if (memcmp(config, KEY_MULTI_ACKS, 10) == 0) {
        sprintf(reply, "> %d", (uint32_t) _prefs->multi_acks);
      } else if (memcmp(config, KEY_ALLOW_READ_ONLY, 15) == 0) {
        sprintf(reply, "> %s", _prefs->allow_read_only ? "on" : "off");
      } else if (memcmp(config, KEY_FLOOD_ADV_INT, 21) == 0) {
        sprintf(reply, "> %d", ((uint32_t) _prefs->flood_advert_interval));
      } else if (memcmp(config, KEY_ADVERT_INTERVAL, 15) == 0) {
        sprintf(reply, "> %d", ((uint32_t) _prefs->advert_interval) * 2);
      } else if (memcmp(config, KEY_GUEST_PASSWORD, 14) == 0) {
        sprintf(reply, "> %s", _prefs->guest_password);
      } else if (memcmp(config, KEY_NAME, 4) == 0) {
        sprintf(reply, "> %s", _prefs->node_name);
      } else if (memcmp(config, KEY_REPEAT, 6) == 0) {
        sprintf(reply, "> %s", _prefs->disable_fwd ? "off" : "on");
      } else if (memcmp(config, KEY_LAT, 3) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lat));
      } else if (memcmp(config, KEY_LON, 3) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lon));
      } else if (memcmp(config, KEY_RADIO, 5) == 0) {
        char freq[16], bw[16];
        strcpy(freq, StrHelper::ftoa(_prefs->freq));
        strcpy(bw, StrHelper::ftoa(_prefs->bw));
        sprintf(reply, "> %s,%s,%d,%d", freq, bw, (uint32_t)_prefs->sf, (uint32_t)_prefs->cr);
      } else if (memcmp(config, KEY_RXDELAY, 7) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->rx_delay_base));
      } else if (memcmp(config, KEY_TXDELAY, 7) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->tx_delay_factor));
      } else if (memcmp(config, KEY_FLOOD_MAX, 9) == 0) {
        sprintf(reply, "> %d", (uint32_t)_prefs->flood_max);
      } else if (memcmp(config, KEY_DIRECT_TXDELAY, 14) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->direct_tx_delay_factor));
      } else if (memcmp(config, KEY_TX, 2) == 0 && (config[2] == 0 || config[2] == ' ')) {
        sprintf(reply, "> %d", (uint32_t) _prefs->tx_power_dbm);
      } else if (memcmp(config, KEY_FREQ, 4) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->freq));
      } else if (memcmp(config, KEY_PUBLIC_KEY, 10) == 0) {
        strcpy(reply, "> ");
        mesh::Utils::toHex(&reply[2], _callbacks->getSelfIdPubKey(), PUB_KEY_SIZE);
      } else if (memcmp(config, KEY_ROLE, 4) == 0) {
        sprintf(reply, "> %s", _callbacks->getRole());
      } else {
        sprintf(reply, "??: %s", config);
      }
    } else if (memcmp(command, CMD_SET, 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, KEY_AF " ", 3) == 0) {
        _prefs->airtime_factor = atof(&config[3]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_INT_THRESH " ", 11) == 0) {
        _prefs->interference_threshold = atoi(&config[11]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_AGC_RESET_INTERVAL " ", 19) == 0) {
        _prefs->agc_reset_interval = atoi(&config[19]) / 4;
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_MULTI_ACKS " ", 11) == 0) {
        _prefs->multi_acks = atoi(&config[11]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_ALLOW_READ_ONLY " ", 16) == 0) {
        _prefs->allow_read_only = memcmp(&config[16], "on", 2) == 0;
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_FLOOD_ADV_INT " ", 22) == 0) {
        int hours = _atoi(&config[22]);
        if ((hours > 0 && hours < 3) || (hours > 48)) {
          strcpy(reply, "Error: interval range is 3-48 hours");
        } else {
          _prefs->flood_advert_interval = (uint8_t)(hours);
          _callbacks->updateFloodAdvertTimer();
          savePrefs();
          strcpy(reply, "OK");
        }
      } else if (memcmp(config, KEY_ADVERT_INTERVAL " ", 16) == 0) {
        int mins = _atoi(&config[16]);
        if ((mins > 0 && mins < MIN_LOCAL_ADVERT_INTERVAL) || (mins > 240)) {
          sprintf(reply, "Error: interval range is %d-240 minutes", MIN_LOCAL_ADVERT_INTERVAL);
        } else {
          _prefs->advert_interval = (uint8_t)(mins / 2);
          _callbacks->updateAdvertTimer();
          savePrefs();
          strcpy(reply, "OK");
        }
      } else if (memcmp(config, KEY_GUEST_PASSWORD " ", 15) == 0) {
        StrHelper::strncpy(_prefs->guest_password, &config[15], sizeof(_prefs->guest_password));
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_NAME " ", 5) == 0) {
        StrHelper::strncpy(_prefs->node_name, &config[5], sizeof(_prefs->node_name));
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_REPEAT " ", 7) == 0) {
        _prefs->disable_fwd = memcmp(&config[7], "off", 3) == 0;
        savePrefs();
        strcpy(reply, _prefs->disable_fwd ? "OK - repeat is now OFF" : "OK - repeat is now ON");
      } else if (memcmp(config, KEY_RADIO " ", 6) == 0) {
        strcpy(tmp, &config[6]);
        const char *parts[4];
        int num = mesh::Utils::parseTextParts(tmp, parts, 4);
        float freq  = num > 0 ? atof(parts[0]) : 0.0f;
        float bw    = num > 1 ? atof(parts[1]) : 0.0f;
        uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
        uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
        if (freq >= 300.0f && freq <= 2500.0f && sf >= 7 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
          _prefs->sf = sf;
          _prefs->cr = cr;
          _prefs->freq = freq;
          _prefs->bw = bw;
          _callbacks->savePrefs();
          strcpy(reply, "OK - reboot to apply");
        } else {
          strcpy(reply, "Error, invalid radio params");
        }
      } else if (memcmp(config, KEY_LAT " ", 4) == 0) {
        _prefs->node_lat = atof(&config[4]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_LON " ", 4) == 0) {
        _prefs->node_lon = atof(&config[4]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, KEY_RXDELAY " ", 8) == 0) {
        float db = atof(&config[8]);
        if (db >= 0) {
          _prefs->rx_delay_base = db;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, KEY_TXDELAY " ", 8) == 0) {
        float f = atof(&config[8]);
        if (f >= 0) {
          _prefs->tx_delay_factor = f;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, KEY_FLOOD_MAX " ", 10) == 0) {
        uint8_t m = atoi(&config[10]);
        if (m <= 64) {
          _prefs->flood_max = m;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, max 64");
        }
      } else if (memcmp(config, KEY_DIRECT_TXDELAY " ", 15) == 0) {
        float f = atof(&config[15]);
        if (f >= 0) {
          _prefs->direct_tx_delay_factor = f;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, KEY_TX " ", 3) == 0) {
        _prefs->tx_power_dbm = atoi(&config[3]);
        savePrefs();
        _callbacks->setTxPower(_prefs->tx_power_dbm);
        strcpy(reply, "OK");
      } else if (sender_timestamp == 0 && memcmp(config, KEY_FREQ " ", 5) == 0) {
        _prefs->freq = atof(&config[5]);
        savePrefs();
        strcpy(reply, "OK - reboot to apply");
      } else {
        sprintf(reply, "unknown config: %s", config);
      }
    } else if (sender_timestamp == 0 && strcmp(command, CMD_ERASE) == 0) {
      bool s = _callbacks->formatFileSystem();
      sprintf(reply, "File system erase: %s", s ? "OK" : "Err");
    } else if (memcmp(command, CMD_VER, 3) == 0) {
      sprintf(reply, "%s (Build: %s)", _callbacks->getFirmwareVer(), _callbacks->getBuildDate());
    } else if (memcmp(command, CMD_LOG_START, 9) == 0) {
      _callbacks->setLoggingOn(true);
      strcpy(reply, "   logging on");
    } else if (memcmp(command, CMD_LOG_STOP, 8) == 0) {
      _callbacks->setLoggingOn(false);
      strcpy(reply, "   logging off");
    } else if (memcmp(command, CMD_LOG_ERASE, 9) == 0) {
      _callbacks->eraseLogFile();
      strcpy(reply, "   log erased");
    } else if (sender_timestamp == 0 && memcmp(command, CMD_LOG_DUMP, 3) == 0) {
      _callbacks->dumpLogFile();
      strcpy(reply, "   EOF");
    } else {
      strcpy(reply, "Unknown command");
    }
}
