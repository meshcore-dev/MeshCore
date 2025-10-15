#pragma once

#include <Arduino.h>
#include <MeshCore.h>

class GAT562MeshTrialTrackerBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  uint8_t btn_prev_state;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(BATTERY_PIN);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char *getManufacturerName() const override {
#if defined(GAT562_MESH_TRACKER_PRO)
    return "GAT562 TRACKER PRO";
#else
    return "GAT562 TRIAL TRACKER";
#endif
  }

  void reboot() override { NVIC_SystemReset(); }

  void powerOff() override {

#ifdef PIN_USER_BTN
    while (digitalRead(PIN_USER_BTN) == LOW) {
      delay(10);
    }
#endif

#ifdef PIN_3V3_EN
    digitalWrite(PIN_3V3_EN, LOW);
    pinMode(PIN_3V3_EN, OUTPUT);
#endif

#ifdef PIN_VIBRATION
    digitalWrite(PIN_VIBRATION, LOW);
    pinMode(PIN_VIBRATION, INPUT);
#endif

#ifdef PIN_LED1
    digitalWrite(PIN_LED1, LOW);
#endif

#ifdef PIN_LED2
    digitalWrite(PIN_LED2, LOW);
#endif

#ifdef PIN_USER_BTN
    nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_USER_BTN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

    sd_power_system_off();
  }

  bool startOTAUpdate(const char *id, char reply[]) override;
};
