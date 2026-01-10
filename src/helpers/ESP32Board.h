#pragma once

#include <MeshCore.h>
#include <Arduino.h>

// Default button polarity: Active-LOW (pressed = LOW)
// Most LoRa boards use GPIO as a boot button that pulls to GND when pressed.
// Override with -D USER_BTN_PRESSED=HIGH in platformio.ini for rare active-high devices.
#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>
#include <Wire.h>
#include "esp_wifi.h"
#include "driver/rtc_io.h"

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;

  #ifdef ESP32_CPU_FREQ
    setCpuFrequencyMhz(ESP32_CPU_FREQ);
  #endif

  #ifdef PIN_VBAT_READ
    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
  #endif

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
   #if PIN_BOARD_SDA >= 0 && PIN_BOARD_SCL >= 0
    Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);
   #endif
  #else
    Wire.begin();
  #endif
  }

  // Temperature from ESP32 MCU
  float getMCUTemperature() override {
    uint32_t raw = 0;

    // To get and average the temperature so it is more accurate, especially in low temperature
    for (int i = 0; i < 4; i++) {
      raw += temperatureRead();
    }

    return raw / 4;
  }

  void enterLightSleep(uint32_t secs, int pin_wake_btn = -1, bool btn_active_high = true) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(P_LORA_DIO_1) // Supported ESP32 variants
    if (rtc_gpio_is_valid_gpio((gpio_num_t)P_LORA_DIO_1)) { // Only enter sleep mode if P_LORA_DIO_1 is RTC pin
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

      // Configure wakeup sources: LoRa packet (always active-high) and optionally button
      if (pin_wake_btn < 0) {
        // No button - just wake on LoRa packet
        esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
      } else if (btn_active_high) {
        // Button is active-high (same as LoRa) - can use single ext1 source
        esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1) | (1ULL << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);
      } else {
        // Button is active-low (different from LoRa) - use ext0 for button, ext1 for LoRa
        esp_sleep_enable_ext0_wakeup((gpio_num_t)pin_wake_btn, 0); // Wake when button goes LOW
        esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH); // Wake on LoRa packet
      }

      if (secs > 0) {
        esp_sleep_enable_timer_wakeup(secs * 1000000); // To wake up after specified seconds
      }

      esp_light_sleep_start(); // CPU enters light sleep
    }
#endif
  }

  void sleep(uint32_t secs) override {
    // To check for WiFi status to see if there is active OTA
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);

    if (err != ESP_OK) {          // WiFi is off ~ No active OTA, safe to go to sleep
#ifdef PIN_USER_BTN
      // Sleep with button wake support (USER_BTN_PRESSED defaults to LOW if not defined)
  #if USER_BTN_PRESSED == LOW
      enterLightSleep(secs, PIN_USER_BTN, false); // Button is active-low
  #else
      enterLightSleep(secs, PIN_USER_BTN, true);  // Button is active-high
  #endif
#else
      enterLightSleep(secs);
#endif
    }
  }

  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#elif defined(P_LORA_TX_NEOPIXEL_LED)
  #define NEOPIXEL_BRIGHTNESS    64  // white brightness (max 255)

  void onBeforeTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS);   // turn TX neopixel on (White)
  }
  void onAfterTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, 0, 0);   // turn TX neopixel off
  }
#endif

  uint16_t getBattMilliVolts() override {
  #ifdef PIN_VBAT_READ
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;

    return (2 * raw);
  #else
    return 0;  // not supported
  #endif
  }

  const char* getManufacturerName() const override {
    return "Generic ESP32";
  }

  void reboot() override {
    esp_restart();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};

class ESP32RTCClock : public mesh::RTCClock {
public:
  ESP32RTCClock() { }
  void begin() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON) {
      // start with some date/time in the recent past
      struct timeval tv;
      tv.tv_sec = 1715770351;  // 15 May 2024, 8:50pm
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
    }
  }
  uint32_t getCurrentTime() override {
    time_t _now;
    time(&_now);
    return _now;
  }
  void setCurrentTime(uint32_t time) override { 
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
};

#endif
