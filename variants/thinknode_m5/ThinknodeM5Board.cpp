#include "ThinknodeM5Board.h"

PCA9557 expander (0x18, &Wire1);

void ThinknodeM5Board::begin() {
    // Start expander and configure pins
    Wire1.begin(48, 47);
    expander.pinMode(EXP_PIN_POWER, OUTPUT); // eink
    expander.pinMode(EXP_PIN_BACKLIGHT, OUTPUT); // peripherals
    expander.pinMode(EXP_PIN_LED, OUTPUT); // peripherals
    expander.digitalWrite(EXP_PIN_POWER, HIGH);
    expander.digitalWrite(EXP_PIN_BACKLIGHT, LOW);
    expander.digitalWrite(EXP_PIN_LED, LOW);

#ifdef PIN_GPS_SWITCH
    pinMode(PIN_GPS_SWITCH, INPUT);
#endif

    ESP32Board::begin();
  }

  void ThinknodeM5Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_deep_sleep_start();
  }

  void ThinknodeM5Board::powerOff()  {
    enterDeepSleep(0);
  }

 uint16_t ThinknodeM5Board::getBattMilliVolts() {
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

  const char* ThinknodeM5Board::getManufacturerName() const {
    return "Elecrow ThinkNode M2";
  }
