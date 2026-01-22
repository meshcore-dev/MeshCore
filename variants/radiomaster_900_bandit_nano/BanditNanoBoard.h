#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

/*
  Pin connections from ESP32-D0WDQ6 to SX1276.
*/
#define P_LORA_DIO_0 22
#define P_LORA_DIO_1 21
#define P_LORA_NSS 4
#define P_LORA_RESET 5
#define P_LORA_SCLK 18
#define P_LORA_MISO 19
#define P_LORA_MOSI 23 
#define SX176X_TXEN 33

#define MAX_LORA_TX_POWER 30
#define LORA_TX_POWER 12	// Value not used, LORA module is fixed to 12dbm so PA is not overdriven

#define P_LORA_TX_LED 15	// LED GPIO 15

/*
  I2C SDA and SCL.
*/
#define PIN_BOARD_SDA 14
#define PIN_BOARD_SCL 12

#define PIN_USER_BTN 39		// User button Five-way, see JOYSTICK_ADC_VALS
#define JOYSTICK_ADC_VALS /*UP*/ 3227, /*DOWN*/ 0, /*LEFT*/ 1961, /*RIGHT*/ 2668, /*OK*/ 1290, /*IDLE*/ 4095

/*
  This unit has a FAN built-in.
  FAN is active at 250mW on it's ExpressLRS Firmware.
  Always ON
*/
#define PA_FAN_EN 2		// FAN on GPIO 2

/*
  This module has Skyworks SKY66122 controlled by dacWrite
  power rangeing from 100mW to 1000mW.

  Mapping of PA_LEVEL to Power output: GPIO26/dacWrite
  168 -> 100mW  -> 2.11v
  148 -> 250mW  -> 1.87v
  128 -> 500mW  -> 1.63v
  90  -> 1000mW -> 1.16v
*/
#define PIN_PA_EN 26		// GPIO 26 enables the PA
#define PA_DAC_EN		// Used DAC Levels
#define PA_LEVEL 90		// If power output table is not found, use this value (1000mW)

#define DISPLAY_ROTATION 2	// Display is flipped

class BanditNanoBoard : public ESP32Board {
private:

public:

  void begin() {
    ESP32Board::begin();

    periph_power.begin();

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

  const char* getManufacturerName() const override {
    return "RadioMaster Bandit Nano";
  }
};
