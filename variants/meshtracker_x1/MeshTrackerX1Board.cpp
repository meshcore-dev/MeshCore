#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "MeshTrackerX1Board.h"
#include "target.h"

void MeshTrackerX1Board::begin() {
  NRF52BoardDCDC::begin();
  btn_prev_state = LOW;   // button is active HIGH

#ifdef BUTTON_PIN
  pinMode(BATTERY_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(LED_PIN, OUTPUT);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  delay(10);   // give lr2021 some time to power up
}

void MeshTrackerX1Board::shutdownPeripherals() {
  sensors.prepareForShutdown();  // PAIR650 must be sent while GPS is powered.
  // Put LR2021 to sleep while its SPI bus and power are still available.
  // Also stops the location provider and releases the shared board resources.
  NRF52Board::shutdownPeripherals();
  SPI.end();  // NSS remains HIGH so the radio is not woken again.

  // Stop UART before disconnecting the GPS pins. Uart::end() must only be
  // called after begin(), otherwise it can wait forever for STOP events.
  if (Serial1) {
    Serial1.end();
  }
  nrf_gpio_cfg_default(digitalPinToPinName(GPS_RX_PIN));
  nrf_gpio_cfg_default(digitalPinToPinName(GPS_TX_PIN));
  nrf_gpio_cfg_default(digitalPinToPinName(GPS_SLEEP_INT));
  nrf_gpio_cfg_default(digitalPinToPinName(GPS_RTC_INT));
  digitalWrite(GPS_RESET, LOW);
  digitalWrite(GPS_EN, LOW);
  // Match the reference variant_shutdown(): keep GPS backup power enabled.
  // initVariant() set GPS_VRTC_EN HIGH; do not toggle it at shutdown.

  // Release I2C pull-ups before disabling the haptic driver, avoiding
  // feeding an unpowered peripheral through SDA/SCL.
  Wire.end();
  nrf_gpio_cfg_default(digitalPinToPinName(PIN_WIRE_SDA));
  nrf_gpio_cfg_default(digitalPinToPinName(PIN_WIRE_SCL));
  digitalWrite(PIN_DRV_EN, LOW);
  digitalWrite(PIN_BAT_ADC_EN, LOW);
  digitalWrite(PIN_RTC_EN, LOW);
  // The reference also retains the shared 3V3 enable during shutdown.
  // Preserve that rail state along with VRTC instead of power-gating it here.

  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);

  // Keep QSPI flash power/configuration unchanged: companion stores contacts
  // and channels there. Power-gating it requires a separate storage shutdown.
}

void MeshTrackerX1Board::powerOff() {
  // The button is active HIGH. Do not arm wake until the user releases it.
  digitalWrite(LED_PIN, HIGH);
  while (digitalRead(BUTTON_PIN) == USER_BTN_PRESSED) {
    delay(1);
  }
  digitalWrite(LED_PIN, LOW);
  nrf_gpio_cfg_sense_input(digitalPinToPinName(BUTTON_PIN),
                          NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_SENSE_HIGH);

  // Dispatches our shutdownPeripherals() then enters SYSTEMOFF, with or
  // without SoftDevice (BLE and USB companion respectively).
  NRF52Board::powerOff();
}
