#include <Arduino.h>
#include "CommonCLI.h"
#include "TxtDataHelpers.h"
#include "AdvertDataHelpers.h"
#include <RTClib.h>

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
    file.read((uint8_t *)pad, 1);                                                     // 79  was 'unused'
    file.read((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.read((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.read((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.read((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.read(pad, 4);                                                                             // 108
    file.read((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.read((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.read((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.read((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.read((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.read((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.read(pad, 3);                                                                             // 121
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
    file.read((uint8_t *)&_prefs->adc_multiplier, sizeof(_prefs->adc_multiplier)); // 166
    file.read((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));  // 170
    // 290

    // sanitise bad pref values
    _prefs->rx_delay_base = constrain(_prefs->rx_delay_base, 0, 20.0f);
    _prefs->tx_delay_factor = constrain(_prefs->tx_delay_factor, 0, 2.0f);
    _prefs->direct_tx_delay_factor = constrain(_prefs->direct_tx_delay_factor, 0, 2.0f);
    _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 9.0f);
    _prefs->freq = constrain(_prefs->freq, 400.0f, 2500.0f);
    _prefs->bw = constrain(_prefs->bw, 7.8f, 500.0f);
    _prefs->sf = constrain(_prefs->sf, 5, 12);
    _prefs->cr = constrain(_prefs->cr, 5, 8);
    _prefs->tx_power_dbm = constrain(_prefs->tx_power_dbm, 1, 30);
    _prefs->multi_acks = constrain(_prefs->multi_acks, 0, 1);
    _prefs->adc_multiplier = constrain(_prefs->adc_multiplier, 0.0f, 10.0f);

    // sanitise bad bridge pref values
    _prefs->bridge_enabled = constrain(_prefs->bridge_enabled, 0, 1);
    _prefs->bridge_delay = constrain(_prefs->bridge_delay, 0, 10000);
    _prefs->bridge_pkt_src = constrain(_prefs->bridge_pkt_src, 0, 1);
    _prefs->bridge_baud = constrain(_prefs->bridge_baud, 9600, 115200);
    _prefs->bridge_channel = constrain(_prefs->bridge_channel, 0, 14);

    _prefs->powersaving_enabled = constrain(_prefs->powersaving_enabled, 0, 1);

    _prefs->gps_enabled = constrain(_prefs->gps_enabled, 0, 1);
    _prefs->advert_loc_policy = constrain(_prefs->advert_loc_policy, 0, 2);

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
    file.write((uint8_t *)pad, 1);                                                     // 79  was 'unused'
    file.write((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.write((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.write((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.write((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.write(pad, 4);                                                                             // 108
    file.write((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.write((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.write((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.write((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.write((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.write((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.write(pad, 3);                                                                             // 121
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
    file.write((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));  // 170
    // 290

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

void CommonCLI::handleCommand(uint32_t sender_timestamp, const char* command, char* reply) {
    if (memcmp(command, "reboot", 6) == 0) {
      _board->reboot();  // doesn't return
    } else if (memcmp(command, "clkreboot", 9) == 0) {
      // Reset clock
      getRTCClock()->setCurrentTime(1715770351);  // 15 May 2024, 8:50pm
      _board->reboot();  // doesn't return
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
      if (freq >= 300.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
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
    /*
     * GET commands
     */
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
      } else if (memcmp(config, "direct.txdelay", 14) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->direct_tx_delay_factor));
      } else if (memcmp(config, "owner.info", 10) == 0) {
        *reply++ = '>';
        *reply++ = ' ';
        const char* sp = _prefs->owner_info;
        while (*sp) {
          *reply++ = (*sp == '\n') ? '|' : *sp;    // translate newline back to orig '|'
          sp++;
        }
        *reply = 0;  // set null terminator
      } else if (memcmp(config, "tx", 2) == 0 && (config[2] == 0 || config[2] == ' ')) {
        sprintf(reply, "> %d", (uint32_t) _prefs->tx_power_dbm);
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
      } else if (memcmp(config, "direct.txdelay ", 15) == 0) {
        float f = atof(&config[15]);
        if (f >= 0) {
          _prefs->direct_tx_delay_factor = f;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
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
        if (baud >= 9600 && baud <= 115200) {
          _prefs->bridge_baud = (uint32_t)baud;
          _callbacks->restartBridge();
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error: baud rate must be between 9600-115200");
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
      } else if (memcmp(command+11, "prefs", 4) == 0) {
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
      _prefs->powersaving_enabled = 1;
      savePrefs();
      strcpy(reply, "ok"); // TODO: to return Not supported if required
    } else if (memcmp(command, "powersaving off", 15) == 0) {
      _prefs->powersaving_enabled = 0;
      savePrefs();
      strcpy(reply, "ok");
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
