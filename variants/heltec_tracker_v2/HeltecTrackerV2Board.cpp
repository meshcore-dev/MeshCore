#include "HeltecTrackerV2Board.h"

void HeltecTrackerV2Board::begin() {
    ESP32Board::begin();

    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive

    // ---- GC1109 RF FRONT END CONFIGURATION ----
    // The Heltec Tracker V2 uses a GC1109 FEM chip with integrated PA and LNA
    // RF path: SX1262 -> GC1109 PA -> Pi attenuator -> Antenna
    // Measured net TX gain (non-linear due to PA compression):
    //   +11dB at 0-15dBm input  (e.g., 10dBm in -> 21dBm out)
    //   +10dB at 16-17dBm input
    //   +9dB  at 18-19dBm input
    //   +7dB  at 21dBm input    (e.g., 21dBm in -> 28dBm out max)
    // Control logic (from GC1109 datasheet):
    //   Shutdown:        CSD=0, CTX=X, CPS=X
    //   Receive LNA:     CSD=1, CTX=0, CPS=X  (17dB gain, 2dB NF)
    //   Transmit bypass: CSD=1, CTX=1, CPS=0  (~1dB loss, no PA)
    //   Transmit PA:     CSD=1, CTX=1, CPS=1  (full PA enabled)
    // Pin mapping:
    //   CTX (pin 6)  -> SX1262 DIO2: TX/RX path select (automatic via SX126X_DIO2_AS_RF_SWITCH)
    //   CSD (pin 4)  -> GPIO4: Chip enable (HIGH=on, LOW=shutdown)
    //   CPS (pin 5)  -> GPIO46: PA mode select (HIGH=full PA, LOW=bypass)
    //   VCC0/VCC1    -> Vfem via LDO, controlled by GPIO7

    // VFEM_Ctrl (GPIO7): Power enable for GC1109 LDO
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, HIGH);

    // CSD (GPIO4): Chip enable - must be HIGH to enable GC1109
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_EN);
    pinMode(P_LORA_PA_EN, OUTPUT);
    digitalWrite(P_LORA_PA_EN, HIGH);

    // CPS (GPIO46): PA mode - LOW for RX (don't care), HIGH during TX for full PA
    // Note: GPIO46 is NOT an RTC GPIO, so no rtc_gpio_hold_dis needed
    pinMode(P_LORA_PA_TX_EN, OUTPUT);
    digitalWrite(P_LORA_PA_TX_EN, LOW);  // Start in RX-ready state
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

  void HeltecTrackerV2Board::onBeforeTransmit(void) {
    digitalWrite(P_LORA_TX_LED, HIGH);     // Turn TX LED on
    digitalWrite(P_LORA_PA_TX_EN, HIGH);   // CPS=1: Enable full PA mode (+30dBm)
    // CTX (TX/RX path) handled by SX1262 DIO2 -> GC1109 CTX (hardware)
  }

  void HeltecTrackerV2Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_PA_TX_EN, LOW);    // CPS=0: Back to RX mode (CPS=X for RX)
    digitalWrite(P_LORA_TX_LED, LOW);      // Turn TX LED off
    // CTX (TX/RX path) handled by SX1262 DIO2 -> GC1109 CTX (hardware)
  }

  void HeltecTrackerV2Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
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

  void HeltecTrackerV2Board::powerOff()  {
    enterDeepSleep(0);
  }

  uint16_t HeltecTrackerV2Board::getBattMilliVolts()  {
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

  const char* HeltecTrackerV2Board::getManufacturerName() const {
    return "Heltec Tracker V2";
  }
