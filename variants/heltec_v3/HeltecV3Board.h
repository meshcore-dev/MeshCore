#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>

#if defined(ESP32)
  #include "nvs_flash.h"
  #include "nvs.h"
#endif

// built-ins
#ifndef PIN_VBAT_READ              // set in platformio.ini for boards like Heltec Wireless Paper (20)
  #define  PIN_VBAT_READ    1
#endif
#ifndef PIN_ADC_CTRL              // set in platformio.ini for Heltec Wireless Tracker (2)
  #define  PIN_ADC_CTRL    37
#endif
#define  PIN_ADC_CTRL_ACTIVE    LOW
#define  PIN_ADC_CTRL_INACTIVE  HIGH

#include <driver/rtc_io.h>

class HeltecV3Board : public ESP32Board {
private:
  bool adc_active_state;
  bool initNvs() {
  #if defined(ESP32)
    static bool nvs_ready = false;
    if (nvs_ready) return true;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
    }
    nvs_ready = (err == ESP_OK);
    return nvs_ready;
  #else
    return false;
  #endif
  }

  bool readNvsU8(const char* key, uint8_t& value) {
  #if defined(ESP32)
    if (!initNvs()) return false;
    nvs_handle_t handle;
    if (nvs_open("heltec_v3", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_get_u8(handle, key, &value);
    nvs_close(handle);
    return err == ESP_OK;
  #else
    (void)key;
    (void)value;
    return false;
  #endif
  }

  void writeNvsU8(const char* key, uint8_t value) {
  #if defined(ESP32)
    if (!initNvs()) return;
    nvs_handle_t handle;
    if (nvs_open("heltec_v3", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
  #else
    (void)key;
    (void)value;
  #endif
  }

public:
  RefCountedDigitalPin periph_power;
  bool screen_enabled;
  bool led_enabled;
  uint8_t screen_brightness;
  mutable char brightness_str[4];

  #ifdef PIN_VEXT_EN_ACTIVE
  HeltecV3Board() : periph_power(PIN_VEXT_EN, PIN_VEXT_EN_ACTIVE), screen_enabled(true), led_enabled(true), screen_brightness(255) {
  #else
  HeltecV3Board() : periph_power(PIN_VEXT_EN), screen_enabled(true), led_enabled(true), screen_brightness(255) {
  #endif
    brightness_str[0] = '2';
    brightness_str[1] = '5';
    brightness_str[2] = '5';
    brightness_str[3] = 0;
  }

  void begin() {
    ESP32Board::begin();

    // Load settings from NVS (ESP32)
    uint8_t val;
    if (readNvsU8("screen", val)) {
      screen_enabled = (val != 0);
    }
    if (readNvsU8("led", val)) {
      led_enabled = (val != 0);
    }
    if (readNvsU8("brightness", val)) {
      screen_brightness = val;
    } else {
      screen_brightness = 255;
      writeNvsU8("brightness", screen_brightness);
    }
    if (screen_brightness == 0) {
      screen_brightness = 255;
      writeNvsU8("brightness", screen_brightness);
    }
    Serial.printf("Loaded: screen=%d led=%d brightness=%u\n", screen_enabled, led_enabled, screen_brightness);

    // Auto-detect correct ADC_CTRL pin polarity (different for boards >3.2)
    pinMode(PIN_ADC_CTRL, INPUT);
    adc_active_state = !digitalRead(PIN_ADC_CTRL);

    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, !adc_active_state); // Initially inactive

    periph_power.begin();

    // Initialize LED pin
    #ifdef P_LORA_TX_LED
      pinMode(P_LORA_TX_LED, OUTPUT);
      digitalWrite(P_LORA_TX_LED, LOW);  // Start with LED off
    #endif

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
  }

  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet
    } else {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet OR wake btn
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start();   // CPU halts here and never returns!
  }

  void powerOff() override {
    enterDeepSleep(0);
  }

  uint16_t getBattMilliVolts() override {
    analogReadResolution(10);
    digitalWrite(PIN_ADC_CTRL, adc_active_state);

    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    digitalWrite(PIN_ADC_CTRL, !adc_active_state);

    return (5.42 * (3.3 / 1024.0) * raw) * 1000;
  }

  const char* getManufacturerName() const override {
    return "Heltec V3";
  }

  // Settings support for screen and LED control
  int getNumSettings() const {
    #ifdef DISPLAY_CLASS
      return 3;  // screen + led + brightness for repeaters/clients with display
    #else
      return 1;  // led only
    #endif
  }

  const char* getSettingName(int i) const {
    #ifdef DISPLAY_CLASS
      if (i == 0) return "screen";
      if (i == 1) return "led";
      if (i == 2) return "brightness";
    #else
      if (i == 0) return "led";
    #endif
    return NULL;
  }

  const char* getSettingValue(int i) const {
    #ifdef DISPLAY_CLASS
      if (i == 0) return screen_enabled ? "1" : "0";
      if (i == 1) return led_enabled ? "1" : "0";
      if (i == 2) {
        snprintf(brightness_str, sizeof(brightness_str), "%u", screen_brightness);
        return brightness_str;
      }
    #else
      if (i == 0) return led_enabled ? "1" : "0";
    #endif
    return NULL;
  }

  bool setSettingValue(const char* name, const char* value) {
    bool enable = (strcmp(value, "1") == 0);
    bool changed = false;
    bool brightness_changed = false;
    
    #ifdef DISPLAY_CLASS
    if (strcmp(name, "screen") == 0) {
      screen_enabled = enable;
      changed = true;
    }
    #endif
    
    if (strcmp(name, "led") == 0) {
      led_enabled = enable;
      #ifdef P_LORA_TX_LED
        digitalWrite(P_LORA_TX_LED, enable ? LOW : LOW);  // Keep off when disabled
      #endif
      changed = true;
    }

    #ifdef DISPLAY_CLASS
    if (strcmp(name, "brightness") == 0) {
      int val = atoi(value);
      if (val < 0) val = 0;
      if (val > 255) val = 255;
      screen_brightness = (uint8_t)val;
      brightness_changed = true;
      changed = true;
    }
    #endif
    
    if (changed) {
      // Save to NVS (ESP32)
      if (brightness_changed) {
        writeNvsU8("brightness", screen_brightness);
        Serial.printf("Saved: brightness=%u\n", screen_brightness);
      } else {
        writeNvsU8(name, enable ? 1 : 0);
        Serial.printf("Saved: %s=%d\n", name, enable);
      }
      return true;
    }
    
    return false;
  }

  // Helper to control LED based on settings
  void setLED(bool on) {
    #ifdef P_LORA_TX_LED
      if (led_enabled && on) {
        digitalWrite(P_LORA_TX_LED, HIGH);
      } else {
        digitalWrite(P_LORA_TX_LED, LOW);
      }
    #endif
  }

  // Override transmit callbacks to respect LED setting
  #ifdef P_LORA_TX_LED
  void onBeforeTransmit() override {
    if (led_enabled) {
      digitalWrite(P_LORA_TX_LED, HIGH);  // turn TX LED on only if enabled
    }
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);  // always turn off
  }
  #endif
};
