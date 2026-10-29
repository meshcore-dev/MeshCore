#include "heltec_rcc6.h"

void HeltecRCC6Board::begin() {
  ESP32Board::begin();

  pinMode(PIN_USER_BTN, INPUT_PULLUP);

  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, LOW);

  if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
    gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO &&
        gpio_get_level((gpio_num_t)P_LORA_DIO_1) == HIGH) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }
  }
}

void HeltecRCC6Board::powerOff() {
  enterDeepSleep(0);
}

uint16_t HeltecRCC6Board::getBattMilliVolts() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_2_5db);
  digitalWrite(PIN_ADC_CTRL, ADC_CTRL_ENABLED);
  delay(10);
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw += analogReadMilliVolts(PIN_VBAT_READ);
  }
  raw = raw / 8;
  digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);

  return (adcMultiplier * raw);
}
