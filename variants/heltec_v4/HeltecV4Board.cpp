#include "HeltecV4Board.h"

void HeltecV4Board::begin() {
    ESP32Board::begin();

    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW);

    // Power on FEM LDO — set registers before releasing RTC hold for
    // atomic transition (no glitch on deep sleep wake).
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, HIGH);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_DEEPSLEEP) {
      delay(1);  // FEM startup time after cold power-on
    }

    // Auto-detect FEM type via GPIO2 default pull level.
    // GC1109 CSD: internal pull-down → reads LOW
    // KCT8103L CSD: internal pull-up → reads HIGH
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_EN);
    pinMode(P_LORA_PA_EN, INPUT);
    delay(1);
    is_kct8103l_ = (digitalRead(P_LORA_PA_EN) == HIGH);

    // CSD/enable: HIGH for both FEM types
    pinMode(P_LORA_PA_EN, OUTPUT);
    digitalWrite(P_LORA_PA_EN, HIGH);

    if (is_kct8103l_) {
        // V4.3 — KCT8103L: CTX on GPIO5 controls TX/RX path
        rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_CTX);
        pinMode(P_LORA_PA_CTX, OUTPUT);
        digitalWrite(P_LORA_PA_CTX, LOW);   // RX mode (LNA enabled)
    } else {
        // V4.2 — GC1109: CPS on GPIO46 controls PA mode
        pinMode(P_LORA_PA_TX_EN, OUTPUT);
        digitalWrite(P_LORA_PA_TX_EN, LOW); // RX bypass mode
    }

    periph_power.begin();

    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {
        startup_reason = BD_STARTUP_RX_PACKET;
      }
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  void HeltecV4Board::onBeforeTransmit(void) {
    digitalWrite(P_LORA_TX_LED, HIGH);
    if (is_kct8103l_) {
        digitalWrite(P_LORA_PA_CTX, HIGH);   // CTX: TX path
    } else {
        digitalWrite(P_LORA_PA_TX_EN, HIGH); // CPS: full PA
    }
  }

  void HeltecV4Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_TX_LED, LOW);
    if (is_kct8103l_) {
        digitalWrite(P_LORA_PA_CTX, LOW);    // CTX: RX path (LNA on)
    } else {
        digitalWrite(P_LORA_PA_TX_EN, LOW);  // CPS: bypass
    }
  }

  void HeltecV4Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    // Hold FEM pins during sleep to keep LNA active for RX wake
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_EN);

    if (is_kct8103l_) {
        // Hold CTX LOW during deep sleep for RX wake (LNA enabled)
        digitalWrite(P_LORA_PA_CTX, LOW);
        rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_CTX);
    }

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet
    } else {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet OR wake btn
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start();   // CPU halts here and never returns!
  }

  void HeltecV4Board::powerOff()  {
    enterDeepSleep(0);
  }

  uint16_t HeltecV4Board::getBattMilliVolts()  {
    analogReadResolution(10);
    digitalWrite(PIN_ADC_CTRL, HIGH);
    delay(10);
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    digitalWrite(PIN_ADC_CTRL, LOW);

    return (5.42 * (3.3 / 1024.0) * raw) * 1000;
  }

  const char* HeltecV4Board::getManufacturerName() const {
  #ifdef HELTEC_LORA_V4_TFT
    return is_kct8103l_ ? "Heltec V4.3 TFT" : "Heltec V4 TFT";
  #else
    return is_kct8103l_ ? "Heltec V4.3 OLED" : "Heltec V4 OLED";
  #endif
  }
