#pragma once

#include <Wire.h>
#include <Arduino.h>
#include <M5Cardputer.h>
#include "helpers/ESP32Board.h"

#define PIN_VBAT_READ 10
#define BATTERY_SAMPLES 8

class M5CardputerBoard : public ESP32Board {
public:
  void begin() {
    // Step 1: Enable power to I/O expander on LoRa Cap (GPIO 46)
    pinMode(46, OUTPUT);
    digitalWrite(46, HIGH);
    delay(100);  // Give I/O expander time to power up

    // Step 2: Initialize main I2C (SDA=2, SCL=1) before M5Cardputer keyboard init
    ESP32Board::begin();

    // Step 3: Initialize M5Cardputer hardware with keyboard enabled
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.internal_imu = false;  // No IMU on Cardputer-Adv
    cfg.internal_rtc = true;
    cfg.internal_spk = true;
    cfg.internal_mic = true;
    M5Cardputer.begin(cfg, true);  // true = enable keyboard
    delay(100);
    M5Cardputer.Keyboard.begin();

    M5.In_I2C.writeRegister8(0x43, 0x03, 0x00, 100000);  
    delay(10);
    M5.In_I2C.writeRegister8(0x43, 0x01, 0xFF, 100000);  
    delay(200); 

    Serial.println("LoRa Cap I/O expander (PI4IOE at 0x43 on SDA=8,SCL=9) configured");

    Serial.println("M5Stack Cardputer-Adv initialized");
    Serial.print("Battery voltage: ");
    Serial.print(getBattMilliVolts());
    Serial.println(" mV");
  }

  uint16_t getBattMilliVolts() override {
    #ifdef PIN_VBAT_READ
      analogReadResolution(12);
      uint32_t raw = 0;
      
      for (int i = 0; i < BATTERY_SAMPLES; i++) {
        raw += analogReadMilliVolts(PIN_VBAT_READ);
        delay(1);
      }
      
      raw = raw / BATTERY_SAMPLES;
      // Cardputer has a voltage divider (2:1)
      return (2 * raw);
    #else
      return 0;
    #endif
  }

  const char* getManufacturerName() const override {
    return "M5Stack Cardputer-Adv";
  }

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    // Configure deep sleep with LoRa interrupt wake
    esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
    
    if (pin_wake_btn >= 0) {
      esp_sleep_enable_ext1_wakeup((1ULL << pin_wake_btn) | (1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
    }
    
    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
    }
    
    Serial.println("Entering deep sleep...");
    Serial.flush();
    esp_deep_sleep_start();
  }
};
