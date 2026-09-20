#include <Arduino.h>
#include <Wire.h>

#include "MuziBaseBoard.h"

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void MuziBaseBoard::initiateShutdown(uint8_t reason) {
  // Disable LoRa module power before shutdown
  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void MuziBaseBoard::begin() {
  NRF52BoardDCDC::begin();
  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_BATTERY_CHARGING, INPUT);
  pinMode(PIN_CHARGER_FAULT, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  // output latches default to HIGH, so pull these low right after enabling
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  // gps power is driven by the sensor manager (mode switch). off to start.
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, LOW);
  // 12V rail is only needed for the superIO display
  pinMode(SCREEN_12V_ENABLE, OUTPUT);
#ifdef MUZI_BASE_SUPERIO
  digitalWrite(SCREEN_12V_ENABLE, HIGH); // Enable 12V power for SH1107 display
  delay(250);
#else
  digitalWrite(SCREEN_12V_ENABLE, LOW);
#endif
  Wire.begin();
  // delay(1000); // wait for display to initialize. otherwise it doesn't come up on boot.

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_BUTTON4, INPUT_PULLUP);
  pinMode(PIN_BUTTON5, INPUT_PULLUP);
  pinMode(PIN_BUTTON6, INPUT_PULLUP);

// #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
//   Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
// #endif
#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif
}
