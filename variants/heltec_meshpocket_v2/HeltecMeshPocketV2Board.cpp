#include "HeltecMeshPocketV2Board.h"
#include <nrf_gpio.h>

void HeltecMeshPocketV2Board::begin() {
  NRF52Board::begin();
  powerBank.begin();
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  powerUi.begin();
}

uint16_t HeltecMeshPocketV2Board::getBattMilliVolts() {
  const auto& status = powerBank.snapshot();
  if (status.cw.ready) return status.cw.voltageMv;
  return status.hltLive() ? status.hlt.batteryMv : 0;
}

void HeltecMeshPocketV2Board::setUsbDataEnabled(bool enabled) {
  digitalWrite(PIN_IO_CSA, enabled ? HIGH : LOW);
}

bool HeltecMeshPocketV2Board::toggleUsbData() {
  const bool enabled = !isUsbDataEnabled();
  setUsbDataEnabled(enabled);
  return enabled;
}

bool HeltecMeshPocketV2Board::isUsbDataEnabled() const {
  return digitalRead(PIN_IO_CSA) == HIGH;
}

void HeltecMeshPocketV2Board::shutdownPeripherals() {
  NRF52Board::shutdownPeripherals();
  powerBank.end();
  nrf_gpio_cfg_sense_input(PIN_USER_BTN, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  pinMode(PIN_9006_IO1, INPUT_PULLDOWN);
  // A held-high IO1 must not immediately wake the board from SYSTEM OFF.
  const auto sense = digitalRead(PIN_9006_IO1) ? NRF_GPIO_PIN_SENSE_LOW : NRF_GPIO_PIN_SENSE_HIGH;
  nrf_gpio_cfg_sense_input(PIN_9006_IO1, NRF_GPIO_PIN_PULLDOWN, sense);
}
