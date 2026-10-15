#include <Arduino.h>
#include <Wire.h>

#include "SenseCapSolarBoard.h"
#include <bluefruit.h>

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void SenseCapSolarBoard::initiateShutdown(uint8_t reason) {
  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);

  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, enable_lpcomp ? LOW : HIGH);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void SenseCapSolarBoard::begin() {
  NRF52BoardDCDC::begin();

  pinMode(BATTERY_PIN, INPUT);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);
  analogReadResolution(12);
  analogReference(AR_INTERNAL_3_0);
  delay(50);

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#elif defined(PIN_BUTTON1)
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
#endif

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

#ifdef LED_WHITE
  pinMode(LED_WHITE, OUTPUT);
  digitalWrite(LED_WHITE, LOW);
#endif
#ifdef LED_BLUE
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);
#endif

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif

  delay(10);   // give sx1262 some time to power up
}

void SenseCapSolarBoard::attachDynamicPrefs(KeyValueStore* prefs) {
  _prefs = prefs;
  char val[8];
  val[0] = 0;
  _prefs->getByKey("leds", val, sizeof(val) - 1);
  _leds_off = strcmp(val, "0") == 0;
}

void SenseCapSolarBoard::setLedsEnabled(bool enable) {
  _leds_off = !enable;
#ifdef DISABLE_BLE_LED
  digitalWrite(LED_BLUE, LOW);
#else
  Bluefruit.autoConnLed(enable);
  digitalWrite(LED_BLUE, (enable && Bluefruit.connected() > 0) ? HIGH : LOW);
#endif
}

void SenseCapSolarBoard::loop() {
  NRF52BoardDCDC::loop();
  if (_leds_off) {
    if (!_leds_applied) {  // BLE starts after prefs load and takes the LED back, so stop it once here
      _leds_applied = true;
      Bluefruit.autoConnLed(false);
    }
    digitalWrite(LED_BLUE, LOW);  // a pending blink can still relight it, so hold it off
  }
}

bool SenseCapSolarBoard::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  if (strcmp(command, "get leds") == 0) {
    sprintf(reply, "> %s", _leds_off ? "off" : "on");
    return true;
  }
  if (memcmp(command, "set leds ", 9) == 0) {
    if (strcmp(&command[9], "on") == 0) {
      if (_prefs && !_prefs->setByKey("leds", "1")) {
        strcpy(reply, "Error: failed to save leds setting");
      } else {
        setLedsEnabled(true);
        strcpy(reply, "OK - leds on");
      }
    } else if (strcmp(&command[9], "off") == 0) {
      if (_prefs && !_prefs->setByKey("leds", "0")) {
        strcpy(reply, "Error: failed to save leds setting");
      } else {
        setLedsEnabled(false);
        strcpy(reply, "OK - leds off");
      }
    } else {
      strcpy(reply, "Error: state must be on or off");
    }
    return true;
  }
  return false; // not handled
}
