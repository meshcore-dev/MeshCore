#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>
#include <Wire.h>
#include "driver/rtc_io.h"
#if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(WAVESHARE_ESP32_C6_LP_BASELINE)
  #include "esp_sleep.h"
  #include "driver/gpio.h"
#endif

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  bool inhibit_sleep = false;

public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;

#if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(WAVESHARE_ESP32_C6_LP_BASELINE)
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    if (wakeup == ESP_SLEEP_WAKEUP_EXT1 || wakeup == ESP_SLEEP_WAKEUP_GPIO) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }
#endif

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

  void enterLightSleep(uint32_t secs) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(P_LORA_DIO_1) // Supported ESP32 variants
    if (rtc_gpio_is_valid_gpio((gpio_num_t)P_LORA_DIO_1)) { // Only enter sleep mode if P_LORA_DIO_1 is RTC pin
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
      esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH); // To wake up when receiving a LoRa packet

      if (secs > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL); // To wake up every hour to do periodically jobs
      }

      esp_light_sleep_start(); // CPU enters light sleep
    }
#elif defined(CONFIG_IDF_TARGET_ESP32C6) && defined(WAVESHARE_ESP32_C6_LP_BASELINE)
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  #ifdef P_LORA_DIO_1
    // Prefer EXT1 wake on LP/RTC GPIOs (0..7 on C6); fallback to digital GPIO wake.
    if (esp_sleep_is_valid_wakeup_gpio((gpio_num_t)P_LORA_DIO_1)) {
      esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
    } else {
      gpio_wakeup_enable((gpio_num_t)P_LORA_DIO_1, GPIO_INTR_HIGH_LEVEL);
      esp_sleep_enable_gpio_wakeup(); // Wake up when receiving a LoRa IRQ pulse
    }
  #endif

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL); // Timer fallback wake
    }

    esp_light_sleep_start();
#endif
  }

  void enterDeepSleep(uint32_t secs) {
#if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(WAVESHARE_ESP32_C6_LP_BASELINE)
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  #ifdef P_LORA_DIO_1
    // Deep sleep wake on C6 requires LP/RTC-capable GPIOs (0..7).
    if (esp_sleep_is_valid_wakeup_gpio((gpio_num_t)P_LORA_DIO_1)) {
      esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
    }
  #endif

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
    }

    esp_deep_sleep_start();
#endif
  }

  void sleep(uint32_t secs) override {
    if (!inhibit_sleep) {
  #if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(WAVESHARE_ESP32_C6_LP_BASELINE)
  #if defined(WAVESHARE_ESP32_C6_USE_DEEP_SLEEP) && (WAVESHARE_ESP32_C6_USE_DEEP_SLEEP == 1)
      enterDeepSleep(secs);
  #else
      enterLightSleep(secs);
  #endif
  #else
      enterLightSleep(secs);      // To wake up after "secs" seconds or when receiving a LoRa packet
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

  void setInhibitSleep(bool inhibit) {
    inhibit_sleep = inhibit;
  }
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
