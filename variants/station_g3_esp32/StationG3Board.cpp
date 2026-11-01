#include "StationG3Board.h"

void StationG3Board::powerOff() {
  loRaFEMControl.setSleepModeEnable();
#ifdef P_PA1_EN
  rtc_gpio_hold_en((gpio_num_t)P_PA1_EN);
#endif

#ifdef P_PRIMARY_LNA_EN
  rtc_gpio_hold_en((gpio_num_t)P_PRIMARY_LNA_EN);
#endif

  ESP32Board::powerOff();
}

bool StationG3Board::setLoRaFemLnaEnabled(bool enable) {
  if (!loRaFEMControl.canControlLNA()) {
    return false;
  }
  loRaFEMControl.setLNAEnable(enable);
  return true;
}

bool StationG3Board::isLoRaFemLnaEnabled() const {
  return loRaFEMControl.isLNAEnabled();
}

bool StationG3Board::setLoRaFemPaGainEnabled(bool enable) {
  if (!loRaFEMControl.canControlPAGain()) {
    return false;
  }
  loRaFEMControl.setPAGainEnable(enable);
  return true;
}

bool StationG3Board::isLoRaFemPaGainEnabled() const {
  return loRaFEMControl.isPAGainEnabled();
}

void StationG3Board::attachDynamicPrefs(KeyValueStore* prefs) {
  _prefs = prefs;

  char gain[8];

  gain[0] = 0;
  _prefs->getByKey("fem_rxgain", gain, 7);  // get initial values
  setLoRaFemLnaEnabled(strcmp(gain, "1") == 0);

  gain[0] = 0;
  _prefs->getByKey("fem_txgain", gain, 7);  // get initial values
  setLoRaFemPaGainEnabled(strcmp(gain, "1") == 0);
}

bool StationG3Board::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  if (strcmp(command, "get radio.fem.rxgain") == 0) {
    if (!loRaFEMControl.canControlLNA()) {
      strcpy(reply, "Error: unsupported");
    } else {
      sprintf(reply, "> %s", isLoRaFemLnaEnabled() ? "on" : "off");
    }
    return true;
  }
  if (memcmp(command, "set radio.fem.rxgain ", 21) == 0) {
    if (!loRaFEMControl.canControlLNA()) {
      strcpy(reply, "Error: unsupported");
    } else if (memcmp(&command[21], "on", 2) == 0) {
      if (setLoRaFemLnaEnabled(true)) {
        _prefs->setByKey("fem_rxgain", "1");
        strcpy(reply, "OK - LoRa FEM RX gain on");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else if (memcmp(&command[21], "off", 3) == 0) {
      if (setLoRaFemLnaEnabled(false)) {
        _prefs->setByKey("fem_rxgain", "0");
        strcpy(reply, "OK - LoRa FEM RX gain off");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else {
      strcpy(reply, "Error: state must be on or off");
    }
    return true;
  }

  if (strcmp(command, "get radio.fem.txgain") == 0) {
    if (!loRaFEMControl.canControlPAGain()) {
      strcpy(reply, "Error: unsupported");
    } else {
      sprintf(reply, "> %s", isLoRaFemPaGainEnabled() ? "on" : "off");
    }
    return true;
  }
  if (memcmp(command, "set radio.fem.txgain ", 21) == 0) {
    if (!loRaFEMControl.canControlPAGain()) {
      strcpy(reply, "Error: unsupported");
    } else if (memcmp(&command[21], "on", 2) == 0) {
      if (setLoRaFemPaGainEnabled(true)) {
        _prefs->setByKey("fem_txgain", "1");
        strcpy(reply, "OK - LoRa FEM TX gain on");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM TX gain");
      }
    } else if (memcmp(&command[21], "off", 3) == 0) {
      if (setLoRaFemPaGainEnabled(false)) {
        _prefs->setByKey("fem_txgain", "0");
        strcpy(reply, "OK - LoRa FEM TX gain off");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM TX gain");
      }
    } else {
      strcpy(reply, "Error: state must be on or off");
    }
    return true;
  }

  return false; // not handled
}
