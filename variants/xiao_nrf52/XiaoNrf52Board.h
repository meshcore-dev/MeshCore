#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#ifdef XIAO_NRF52

class XiaoNrf52Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    // Please read befor going further ;)
    // https://wiki.seeedstudio.com/XIAO_BLE#q3-what-are-the-considerations-when-using-xiao-nrf52840-sense-for-battery-charging

    // We can't drive VBAT_ENABLE to HIGH as long
    // as we don't know wether we are charging or not ...
    // this is a 3mA loss (4/1500)
    digitalWrite(VBAT_ENABLE, LOW);
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(PIN_VBAT);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char* getManufacturerName() const override {
    return "Seeed Xiao-nrf52";
  }

  void reboot() override {
    NVIC_SystemReset();
  }

  void powerOff() override {
    // set led on and wait for button release before poweroff
    digitalWrite(PIN_LED, LOW);
#ifdef PIN_USER_BTN
    while(digitalRead(PIN_USER_BTN) == LOW);
#endif
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);
    digitalWrite(PIN_LED, HIGH);

#ifdef PIN_USER_BTN
    // configure button press to wake up when in powered off state
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_USER_BTN]), NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_SENSE_LOW);
#endif

    sd_power_system_off();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};

#endif