#include "ThinknodeM2Board.h"



void ThinknodeM2Board::begin() {
    pinMode(PIN_VEXT_EN, OUTPUT); 
    digitalWrite(PIN_VEXT_EN, !PIN_VEXT_EN_ACTIVE); // force power cycle
    delay(20); // allow power rail to discharge
    digitalWrite(PIN_VEXT_EN, PIN_VEXT_EN_ACTIVE); // turn backlight back on
    delay(120); // give display time to bias on cold boot
    ESP32Board::begin();
    pinMode(PIN_STATUS_LED, OUTPUT); // init power led
  }

  void ThinknodeM2Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_deep_sleep_start();
  }

  void ThinknodeM2Board::powerOff()  {
    enterDeepSleep(0);
  }

 uint16_t ThinknodeM2Board::getBattMilliVolts() {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_VBAT_READ, ADC_11db);

    uint32_t mv = 0;
      for (int i = 0; i < 8; ++i) {
        mv += analogReadMilliVolts(PIN_VBAT_READ);
        delayMicroseconds(200);
      }
    mv /= 8;

    analogReadResolution(10);  
    return static_cast<uint16_t>(mv * ADC_MULTIPLIER );
}

  const char* ThinknodeM2Board::getManufacturerName() const {
    return "Elecrow ThinkNode M2";
  }
