#include "HeltecRC32Board.h"

void HeltecRC32Board::begin() {
  ESP32Board::begin();

  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);

#ifdef SENSOR_RST_PIN
  pinMode(SENSOR_RST_PIN, OUTPUT);
  digitalWrite(SENSOR_RST_PIN, HIGH);
#endif

#ifdef LED_POWER
  pinMode(LED_POWER, OUTPUT);
  digitalWrite(LED_POWER, LOW);
#endif

  periph_power.begin();
  periph_power.claim();

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1L << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }

    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}

void HeltecRC32Board::powerOff() {
  enterDeepSleep(0);
}

void HeltecRC32Board::onBeforeTransmit() {
  digitalWrite(P_LORA_TX_LED, HIGH);
}

void HeltecRC32Board::onAfterTransmit() {
  digitalWrite(P_LORA_TX_LED, LOW);
}

uint16_t HeltecRC32Board::getBattMilliVolts() {
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

  return (adc_mult * raw);
}

bool HeltecRC32Board::setAdcMultiplier(float multiplier) {
  adc_mult = multiplier == 0.0f ? ADC_MULTIPLIER : multiplier;
  return true;
}

const char* HeltecRC32Board::getManufacturerName() const {
  return "Heltec RC32";
}
