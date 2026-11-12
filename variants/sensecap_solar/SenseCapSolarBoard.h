#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class SenseCapSolarBoard : public NRF52BoardDCDC {
#ifdef SENSECAP_SOLAR_WHITE_LED
  // Dimmed white-LED heartbeat state (repeater builds only; enabled with
  // -D SENSECAP_SOLAR_WHITE_LED in the PlatformIO env, so room-server and
  // companion firmwares sharing this board class are unaffected).
  bool white_led_ota_active_ = false;
  bool white_led_pulse_active_ = false;
  uint32_t white_led_last_pulse_at_ = 0;

  void whiteLedHeartbeatOn();    // start 1 kHz PWM at 10% duty on LED_WHITE
  void whiteLedHeartbeatOff();   // stop/release PWM, hold LED_WHITE LOW
  void updateWhiteStatusLed();   // non-blocking 100 ms pulse every 5000 ms
#endif

protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  SenseCapSolarBoard() : NRF52Board("SENSECAP_SOLAR_OTA") {}
  void begin();

#ifdef SENSECAP_SOLAR_WHITE_LED
  // Runs the white-LED heartbeat; invoked by the example main loops via the
  // existing board.loop() call (mesh::MainBoard::loop() is a no-op by default).
  void loop() override;
  bool startOTAUpdate(const char *id, char reply[]) override;
#endif

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    digitalWrite(VBAT_ENABLE, LOW);
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(BATTERY_PIN);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char* getManufacturerName() const override {
    return "Seeed SenseCap Solar";
  }

  void powerOff() override {
#ifdef SENSECAP_SOLAR_WHITE_LED
    whiteLedHeartbeatOff();   // stop/release the heartbeat PWM before shutdown
#endif
    digitalWrite(LED_WHITE, LOW);
    digitalWrite(LED_BLUE, LOW);

#ifdef PIN_USER_BTN
    while (digitalRead(PIN_USER_BTN) == LOW);
    // Keep pull-up enabled in system-off so the wake line doesn't float low.
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_USER_BTN]), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#elif defined(PIN_BUTTON1)
    while (digitalRead(PIN_BUTTON1) == LOW);
    // Keep pull-up enabled in system-off so the wake line doesn't float low.
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(g_ADigitalPinMap[PIN_BUTTON1]), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

#ifdef NRF52_POWER_MANAGEMENT
    initiateShutdown(SHUTDOWN_REASON_USER);
#else
    NRF52Board::powerOff();
#endif
  }
};
