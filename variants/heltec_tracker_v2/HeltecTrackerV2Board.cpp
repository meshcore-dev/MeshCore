#include "HeltecTrackerV2Board.h"

void HeltecTrackerV2Board::begin() {
    ESP32Board::begin();

    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive

    loRaFEMControl.init();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_DEEPSLEEP) {
      delay(1);  // GC1109 startup time after cold power-on
    }

    periph_power.begin();
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
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    loRaFEMControl.setTxModeEnable();
  }

  void HeltecTrackerV2Board::onAfterTransmit(void) {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    loRaFEMControl.setRxModeEnable();
  }

  void HeltecTrackerV2Board::powerOff() {
    // Turn off PA
    digitalWrite(P_LORA_PA_POWER, LOW);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);

    ESP32Board::powerOff();
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

  bool HeltecTrackerV2Board::setLoRaFemLnaEnabled(bool enable) {
    if (!loRaFEMControl.isLnaCanControl()) {
      return false;
    }

    loRaFEMControl.setLNAEnable(enable);
    loRaFEMControl.setRxModeEnable();
    return true;
  }

  bool HeltecTrackerV2Board::canControlLoRaFemLna() const {
    return loRaFEMControl.isLnaCanControl();
  }

  bool HeltecTrackerV2Board::isLoRaFemLnaEnabled() const {
    return loRaFEMControl.isLNAEnabled();
  }
