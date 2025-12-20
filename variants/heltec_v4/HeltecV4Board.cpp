#include "HeltecV4Board.h"

void HeltecV4Board::begin() {
    ESP32Board::begin();


    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive

    // ---- GC1109 RF FRONT END CONFIGURATION ----
    // The Heltec V4 uses a GC1109 FEM chip with integrated PA and LNA
    // RF switch control: PA_TX_EN LOW = RX path (LNA), HIGH = TX path (PA)

    // PA_POWER: Power enable for GC1109 chip (always on)
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, HIGH);

    // PA_EN: Main enable for GC1109 (must be HIGH for both RX and TX)
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_EN);
    pinMode(P_LORA_PA_EN, OUTPUT);
    digitalWrite(P_LORA_PA_EN, HIGH);

    // PA_TX_EN: RF switch control (LOW=RX/LNA, HIGH=TX/PA)
    pinMode(P_LORA_PA_TX_EN, OUTPUT);
    digitalWrite(P_LORA_PA_TX_EN, LOW);  // Default to RX mode
    // -------------------------------------------

    periph_power.begin();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  void HeltecV4Board::onBeforeTransmit(void) {
    digitalWrite(P_LORA_TX_LED, HIGH);    // Turn TX LED on
    digitalWrite(P_LORA_PA_TX_EN, HIGH);  // Switch to TX path (PA)
  }

  void HeltecV4Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_PA_TX_EN, LOW);   // Switch back to RX path (LNA)
    digitalWrite(P_LORA_TX_LED, LOW);     // Turn TX LED off
  }

  void HeltecV4Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    // Hold GC1109 FEM pins during sleep (PA_EN=HIGH, PA_TX_EN=LOW for RX mode)
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_EN);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_TX_EN);

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
    return "Heltec V4";
  }
