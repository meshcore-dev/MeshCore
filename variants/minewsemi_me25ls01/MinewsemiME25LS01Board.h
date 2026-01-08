#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// LoRa and SPI pins

#define  P_LORA_DIO_1   (32 + 12)  // P1.12
#define  P_LORA_NSS     (32 + 13)  // P1.13
#define  P_LORA_RESET   (32 + 11)  // P1.11
#define  P_LORA_BUSY    (32 + 10)  // P1.10
#define  P_LORA_SCLK    (32 + 15)  // P1.15
#define  P_LORA_MISO    (0 + 29)   // P0.29
#define  P_LORA_MOSI    (0 + 2)    // P0.2
 
#define LR11X0_DIO_AS_RF_SWITCH  true
#define LR11X0_DIO3_TCXO_VOLTAGE   1.6

#define  PIN_VBAT_READ BATTERY_PIN
#define  ADC_MULTIPLIER   (1.815f) // dependent on voltage divider resistors. TODO: more accurate battery tracking

class MinewsemiME25LS01Board : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;

public:
  MinewsemiME25LS01Board() : NRF52Board("Minewsemi_OTA") {}
  void begin();

#define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;
    return (ADC_MULTIPLIER * raw);
  }

  const char* getManufacturerName() const override {
    return "Minewsemi";
  }

  void powerOff() override {
    #ifdef HAS_GPS
        digitalWrite(GPS_VRTC_EN, LOW);
        digitalWrite(GPS_RESET, LOW);
        digitalWrite(GPS_SLEEP_INT, LOW);
        digitalWrite(GPS_RTC_INT, LOW);
        pinMode(GPS_RESETB, OUTPUT);
        digitalWrite(GPS_RESETB, LOW);
    #endif
    
    #ifdef BUZZER_EN
        digitalWrite(BUZZER_EN, LOW);
    #endif
    
    #ifdef LED_PIN
    digitalWrite(LED_PIN, LOW);
    #endif
    #ifdef BUTTON_PIN
    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(BUTTON_PIN), NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_HIGH);
    #endif
    sd_power_system_off();
  }

  #if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);// turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW); // turn TX LED off
  }
#endif
};