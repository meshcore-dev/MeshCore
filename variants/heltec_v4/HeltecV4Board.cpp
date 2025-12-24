#include "HeltecV4Board.h"

void HeltecV4Board::begin() {
    ESP32Board::begin();

    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive

    // Check if waking from deep sleep
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      // Release RTC holds - pins retain their state, no need to reconfigure
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);
      rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_EN);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    } else {
      // Cold boot: Configure GC1109 FEM pins
      // Control logic (from GC1109 datasheet):
      //   Receive LNA:  CSD=1, CTX=0, CPS=X  (17dB gain, 2dB NF)
      //   Transmit PA:  CSD=1, CTX=1, CPS=1  (full PA enabled)
      // Pin mapping: CTX->DIO2 (auto), CSD->GPIO2, CPS->GPIO46, VFEM->GPIO7

      // VFEM_Ctrl (GPIO7): Power enable for GC1109 LDO
      pinMode(P_LORA_PA_POWER, OUTPUT);
      digitalWrite(P_LORA_PA_POWER, HIGH);

      // CSD (GPIO2): Chip enable - must be HIGH for GC1109 to work
      pinMode(P_LORA_PA_EN, OUTPUT);
      digitalWrite(P_LORA_PA_EN, HIGH);
    }

    periph_power.begin();

    // Note: GPIO46 (CPS) is a strapping pin - do NOT configure it here.
    // TX handlers are fully responsible for GPIO46 (see onBeforeTransmit/onAfterTransmit)
  }

  void HeltecV4Board::onBeforeTransmit(void) {
    // GPIO46 is a strapping pin - only drive it when actively transmitting
    pinMode(P_LORA_PA_TX_EN, OUTPUT);
    digitalWrite(P_LORA_PA_TX_EN, HIGH);   // CPS=1: Enable full PA mode
    digitalWrite(P_LORA_TX_LED, HIGH);
  }

  void HeltecV4Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_PA_TX_EN, LOW);
    pinMode(P_LORA_PA_TX_EN, INPUT);       // Release strapping pin
    digitalWrite(P_LORA_TX_LED, LOW);
  }

  void HeltecV4Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    // Hold GC1109 FEM pins during sleep for RX wake capability
    // State: CSD=1, CTX=0 (DIO2), CPS=X -> Receive LNA mode
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);   // VFEM_Ctrl - keep LDO powered
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_EN);      // CSD=1 - chip enabled
    // Note: GPIO46 (CPS) is NOT an RTC GPIO, cannot hold - but CPS is don't care for RX

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
    return "Heltec V4 TFT";
  #else
    return "Heltec V4 OLED";
  #endif
  }
