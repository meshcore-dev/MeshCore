#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class muzi_base_duoBoard : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  muzi_base_duoBoard() : NRF52Board("MUZI_BASE_DUO_OTA") {}
  void begin();

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    MESH_DEBUG_PRINTLN("BaseDuo: Sampling battery");
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(BATTERY_PIN);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw) / 4096;
  }

  const char* getManufacturerName() const override {
    return "Muzi Base Duo";
  }

  // int buttonStateChanged() {
  // #ifdef BUTTON_PIN
  //   uint8_t v = digitalRead(BUTTON_PIN);
  //   if (v != btn_prev_state) {
  //     btn_prev_state = v;
  //     return (v == LOW) ? 1 : -1;
  //   }
  // #endif
  //   return 0;
  // }

  // void powerOff() override {
  //   // #ifdef HAS_GPS
  //   //     digitalWrite(GPS_VRTC_EN, LOW);
  //   //     digitalWrite(GPS_RESET, LOW);
  //   //     digitalWrite(GPS_SLEEP_INT, LOW);
  //   //     digitalWrite(GPS_RTC_INT, LOW);
  //   //     digitalWrite(GPS_EN, LOW);
  //   // #endif

  //   // #ifdef BUZZER_EN
  //   //     digitalWrite(BUZZER_EN, LOW);
  //   // #endif

  //   // #ifdef PIN_3V3_EN
  //   //     digitalWrite(PIN_3V3_EN, LOW);
  //   // #endif

  //   // #ifdef PIN_3V3_ACC_EN
  //   //     digitalWrite(PIN_3V3_ACC_EN, LOW);
  //   // #endif
  //   // #ifdef SENSOR_EN
  //   //     digitalWrite(SENSOR_EN, LOW);
  //   // #endif

  //   // set led on and wait for button release before poweroff
  //   #ifdef LED_PIN
  //   digitalWrite(LED_PIN, HIGH);
  //   #endif
  //   #ifdef BUTTON_PIN
  //   while(digitalRead(BUTTON_PIN));
  //   #endif
  //   #ifdef LED_PIN
  //   digitalWrite(LED_PIN, LOW);
  //   #endif

  //   #ifdef BUTTON_PIN
  //   nrf_gpio_cfg_sense_input(BUTTON_PIN, NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_HIGH);
  //   #endif

  //   sd_power_system_off();
  // }
};
