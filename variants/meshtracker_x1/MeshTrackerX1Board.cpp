#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "MeshTrackerX1Board.h"
#include "target.h"
#ifdef QSPIFLASH
#include <CustomLFS_QSPIFlash.h>
#include "MeshTrackerX1Flash.h"

namespace {
struct X1FlashShutdown {
  bool transferIdle() { return QSPIFlash.isReady(); }
  bool readStatus(uint8_t& status) {
    const nrf_qspi_cinstr_conf_t command = {
      .opcode = 0x05, .length = NRF_QSPI_CINSTR_LEN_2B,
      .io2_level = true, .io3_level = true,
      .wipwait = false, .wren = false
    };
    return nrfx_qspi_cinstr_xfer(&command, nullptr, &status) == NRFX_SUCCESS;
  }
  void unmount() { QSPIFlash.end(); }
  void stopBus() {
    nrfx_qspi_uninit();
    QSPIFlash._qspi_initialized = false;
  }
  void releasePins() {
    const uint8_t pins[] = {PIN_QSPI_CS, PIN_QSPI_SCK, PIN_QSPI_IO0,
                           PIN_QSPI_IO1, PIN_QSPI_IO2, PIN_QSPI_IO3};
    for (uint8_t pin : pins) nrf_gpio_cfg_default(digitalPinToPinName(pin));
  }
  void disablePower() { digitalWrite(PIN_FLASH_EN, LOW); }
};
}
#endif

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
  // GPS-only sleep retains backup power; whole-board shutdown removes it.
  digitalWrite(GPS_VRTC_EN, LOW);

  // Release I2C pull-ups before disabling the haptic driver, avoiding
  // feeding an unpowered peripheral through SDA/SCL.
  Wire.end();
  nrf_gpio_cfg_default(digitalPinToPinName(PIN_WIRE_SDA));
  nrf_gpio_cfg_default(digitalPinToPinName(PIN_WIRE_SCL));
  digitalWrite(PIN_DRV_EN, LOW);
  digitalWrite(PIN_BAT_ADC_EN, LOW);
  digitalWrite(PIN_RTC_EN, LOW);
  digitalWrite(PIN_3V3_EN, LOW);

  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_BLUE, LOW);
}

void MeshTrackerX1Board::powerOff() {
  // Finish lazy application writes before unmounting or changing any power rail.
  if (_prepareShutdown && !_prepareShutdown()) {
    Serial.println("X1: shutdown cancelled; pending contacts could not be saved");
    return;
  }
#ifdef QSPIFLASH
  // Without an application save hook, storage may still contain pending writes.
  if (!_prepareShutdown || !QSPIFlash._qspi_initialized) {
    Serial.println("X1: shutdown cancelled; storage is not ready");
    return;
  }
  X1FlashShutdown flash;
  if (!shutdownX1Flash(flash)) {
    Serial.println("X1: shutdown cancelled; flash status unavailable or busy");
    return;
  }
#else
  // These roles do not mount external flash.
  digitalWrite(PIN_FLASH_EN, LOW);
#endif
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
  // SYSTEMOFF must not return to code that could access the unmounted flash.
  for (;;) delay(1000);
}
