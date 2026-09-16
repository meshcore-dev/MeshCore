#include <Arduino.h>
#include <Wire.h>

#include "muzi_base_duoBoard.h"

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void muzi_base_duoBoard::initiateShutdown(uint8_t reason) {
  // Disable LoRa module power before shutdown
  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void muzi_base_duoBoard::begin() {
  NRF52BoardDCDC::begin();
  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(SCREEN_12V_ENABLE, OUTPUT);
  digitalWrite(SCREEN_12V_ENABLE, HIGH); // Enable 12V power for SH1107 display
  delay(250);
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
  // delay(10);   // give LR1121 some time to power up
}