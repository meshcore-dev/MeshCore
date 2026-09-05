#include "CommonRadioPrefs.h"
#include "TxtDataHelpers.h"
#include "Utils.h"
#include "target.h"

bool CommonRadioPrefs::getByKey(const char* key, char* value, size_t max_len) {
  if (strcmp(key, "fem_rxgain") == 0) {
    snprintf(value, max_len, "%d", (uint32_t)getFEMRxGain());
    return true;
  }
  if (strcmp(key, "fem_txgain") == 0) {
    snprintf(value, max_len, "%d", (uint32_t)getFEMTxGain());
    return true;
  }
  return false;
}
bool CommonRadioPrefs::setByKey(const char* key, const char* value) {
  if (strcmp(key, "fem_rxgain") == 0) {
    setFEMRxGain(atoi(value));
    markDirty();
    return true;
  }
  if (strcmp(key, "fem_txgain") == 0) {
    setFEMTxGain(atoi(value));
    markDirty();
    return true;
  }
  return false;
}

bool CommonRadioPrefs::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  if (strcmp(command, "get radio") == 0) {
    char freq[16], bw[16];
    strcpy(freq, StrHelper::ftoa(getFreq()));
    strcpy(bw, StrHelper::ftoa3(getBandwidth()));
    sprintf(reply, "> %s,%s,%d,%d", freq, bw, (uint32_t)getSpreadFactor(), (uint32_t)getCodingRate());
    return true;
  }
  if (memcmp(command, "set radio ", 10) == 0) {
    char tmp[132];
    strcpy(tmp, &command[10]);
    const char *parts[4];
    int num = mesh::Utils::parseTextParts(tmp, parts, 4);
    float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
    float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
    uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
    uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
    if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
      setSpreadFactor(sf);
      setCodingRate(cr);
      setFreq(freq);
      setBandwidth(bw);
      strcpy(reply, "OK - reboot to apply");
    } else {
      strcpy(reply, "Error, invalid radio params");
    }
    return true;
  }

  if (strcmp(command, "get freq") == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(getFreq()));
    return true;
  }

  if (strcmp(command, "get af") == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(getAirtimeFactor()));
    return true;
  }
  if (memcmp(command, "set af ", 7) == 0) {
    setAirtimeFactor(atof(&command[7]));
    strcpy(reply, "OK");
    return true;
  }

  if (strcmp(command, "get dutycycle") == 0) {
    float dc = 100.0f / (getAirtimeFactor() + 1.0f);
    int dc_int = (int)dc;
    int dc_frac = (int)((dc - dc_int) * 10.0f + 0.5f);
    sprintf(reply, "> %d.%d%%", dc_int, dc_frac);
    return true;
  }
  if (memcmp(command, "set dutycycle ", 14) == 0) {
    float dc = atof(&command[14]);
    if (dc < 1 || dc > 100) {
      strcpy(reply, "ERROR: dutycycle must be 1-100");
    } else {
      setAirtimeFactor((100.0f / dc) - 1.0f);
      float actual = 100.0f / (getAirtimeFactor() + 1.0f);
      int a_int = (int)actual;
      int a_frac = (int)((actual - a_int) * 10.0f + 0.5f);
      sprintf(reply, "OK - %d.%d%%", a_int, a_frac);
    }
    return true;
  }

  if (strcmp(command, "get int.thresh") == 0) {
    sprintf(reply, "> %d", (uint32_t) getIntThresh());
    return true;
  }
  if (memcmp(command, "set int.thresh ", 15) == 0) {
    setIntThresh(atoi(&command[15]));
    strcpy(reply, "OK");
    return true;
  }

  if (strcmp(command, "get cad") == 0) {
    sprintf(reply, "> %s", isCadEnabled() ? "on" : "off");
    return true;
  }
  if (memcmp(command, "set cad ", 8) == 0) {
    setCadEnabled(memcmp(&command[8], "on", 2) == 0);
    strcpy(reply, "OK");
    return true;
  }

  if (strcmp(command, "get radio.rxgain") == 0) {
    sprintf(reply, "> %s", getRxGain() != 0 ? "on" : "off");
    return true;
  }
  if (memcmp(command, "set radio.rxgain ", 17) == 0) {
    bool enabled = memcmp(&command[17], "on", 2) == 0;
    setRxGain(enabled);
    if (radio_driver.setRxBoostedGainMode(enabled)) {
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error: unsupported");
    }
    return true;
  }

  if (memcmp(command, "get tx", 6) == 0 && (command[6] == 0 || command[6] == ' ')) {
    sprintf(reply, "> %d", (int32_t) getTxPower());
    return true;
  }
  if (memcmp(command, "set tx ", 7) == 0) {
    setTxPower(atoi(&command[7]));
    radio_driver.setTxPower(getTxPower());
    strcpy(reply, "OK");
    return true;
  }

  if (strcmp(command, "get rxdelay") == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(getRxDelay()));
    return true;
  }
  if (memcmp(command, "set rxdelay ", 12) == 0) {
    float db = atof(&command[12]);
    if (db >= 0 && db <= 20.0f) {
      setRxDelay(db);
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-20");
    }
    return true;
  }

  if (strcmp(command, "get agc.reset.interval") == 0) {
    sprintf(reply, "> %d", (uint32_t) getAgcResetInt());
    return true;
  }
  if (memcmp(command, "set agc.reset.interval ", 23) == 0) {
    setAgcResetInt(atoi(&command[23]));
    sprintf(reply, "OK - interval rounded to %d", (uint32_t) getAgcResetInt());
    return true;
  }

  if (strcmp(command, "get path.hash.mode") == 0) {
    sprintf(reply, "> %d", (uint32_t)getHashMode());
    return true;
  }
  if (memcmp(command, "set path.hash.mode ", 19) == 0) {
    const char* config = command + 19;
    uint8_t mode = atoi(config);
    if (mode < 3) {
      setHashMode(mode);
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0,1, or 2");
    }
    return true;
  }

  if (strcmp(command, "get multi.acks") == 0) {
    sprintf(reply, "> %d", (uint32_t) getMultiAcks());
    return true;
  }
  if (memcmp(command, "set multi.acks ", 15) == 0) {
    setMultiAcks(atoi(&command[15]));
    strcpy(reply, "OK");
    return true;
  }

  if (strcmp(command, "get txdelay") == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(getFloodTxDelay()));
    return true;
  }
  if (memcmp(command, "set txdelay ", 12) == 0) {
    float f = atof(&command[12]);
    if (f >= 0 && f <= 2.0f) {
      setFloodTxDelay(f);
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-2");
    }
    return true;
  }

  if (strcmp(command, "get direct.txdelay") == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(getDirectTxDelay()));
    return true;
  }
  if (memcmp(command, "set direct.txdelay ", 19) == 0) {
    float f = atof(&command[19]);
    if (f >= 0 && f <= 2.0f) {
      setDirectTxDelay(f);
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, must be 0-2");
    }
    return true;
  }

  return false; // not handled
}
