#include <Arduino.h>
#include <Wire.h>

#include "WioTrackerL1Board.h"

void WioTrackerL1Board::begin() {
  NRF52BoardDCDC::begin();
  btn_prev_state = HIGH;

  pinMode(PIN_VBAT_READ, INPUT); // VBAT ADC input
  // Set all button pins to INPUT_PULLUP
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_BUTTON4, INPUT_PULLUP);
  pinMode(PIN_BUTTON5, INPUT_PULLUP);
  pinMode(PIN_BUTTON6, INPUT_PULLUP);

  #if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
    Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
  #endif

  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  delay(10);   // give sx1262 some time to power up
}
 
bool WioTrackerL1Board::setLoRaFemPaGainEnabled(bool enable) {
  _is_pa_enabled = enable;
  digitalWrite(SX126X_POWER_EN, enable ? HIGH : LOW);   // enable/disable the 1W PA
  return true;
}

void WioTrackerL1Board::attachDynamicPrefs(KeyValueStore* prefs) {
  _prefs = prefs;

  char gain[8];

  gain[0] = 0;
  _prefs->getByKey("fem_txgain", gain, 7);  // get initial values
  setLoRaFemPaGainEnabled(strcmp(gain, "1") == 0);
}

bool WioTrackerL1Board::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  if (strcmp(command, "get radio.fem.rxgain") == 0) {
    strcpy(reply, "Error: unsupported");
    return true;
  }
  if (memcmp(command, "set radio.fem.rxgain ", 21) == 0) {
    strcpy(reply, "Error: unsupported");
    return true;
  }

  if (strcmp(command, "get radio.fem.txgain") == 0) {
    sprintf(reply, "> %s", isLoRaFemPaGainEnabled() ? "on" : "off");
    return true;
  }
  if (memcmp(command, "set radio.fem.txgain ", 21) == 0) {
    if (memcmp(&command[21], "on", 2) == 0) {
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
