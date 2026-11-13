#include "HeltecRC52Board.h"

#include <Arduino.h>
#include <Wire.h>

extern void variant_shutdown();

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
    .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
    .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
    .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK};

void HeltecRC52Board::initiateShutdown(uint8_t reason)
{
  shutdownPeripherals();

  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);
  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, enable_lpcomp ? ADC_CTRL_ENABLED : !ADC_CTRL_ENABLED);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif

void HeltecRC52Board::begin()
{
  NRF52BoardDCDC::begin();

  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif
  Wire.begin();

  pinMode(SENSOR_RST_PIN, OUTPUT);
  digitalWrite(SENSOR_RST_PIN, HIGH);
  pinMode(SENSOR_INT, INPUT);


#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW); // off by default, active high

  periph_power.begin();
  periph_power.claim();
}

void HeltecRC52Board::shutdownPeripherals()
{
  NRF52Board::shutdownPeripherals();
  variant_shutdown();
}

void HeltecRC52Board::onBeforeTransmit() {
  digitalWrite(P_LORA_TX_LED, HIGH);
}

void HeltecRC52Board::onAfterTransmit() {
  digitalWrite(P_LORA_TX_LED, LOW);
}

uint16_t HeltecRC52Board::getBattMilliVolts()
{
  analogReadResolution(12);
  analogReference(AR_INTERNAL_3_0);
  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, ADC_CTRL_ENABLED);

  delay(10);
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw += analogRead(PIN_VBAT_READ);
  }
  raw /= 8;
  digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);

  return (uint16_t)((float)raw * MV_LSB * ADC_MULTIPLIER);
}

const char* HeltecRC52Board::getManufacturerName() const
{
  return "Heltec RC52";
}
