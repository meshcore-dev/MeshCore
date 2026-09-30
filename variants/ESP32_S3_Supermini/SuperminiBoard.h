#pragma once

#include <Arduino.h>
#include "target.h"
#include <MeshCore.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>


class SuperminiBoard : public ESP32Board {
private:
	float adc_mult = ADC_MULTIPLIER;
public:
	SuperminiBoard() {}

	void begin() {
		ESP32Board::begin();

		pinMode(PIN_VBAT_READ, INPUT);
		pinMode(BUTTON_PIN, INPUT_PULLUP);

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

		rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
		rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

		rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

		if (pin_wake_btn < 0) {
			esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet
		}
		else {
			esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet OR wake btn
		}

		if (secs > 0) {
			esp_sleep_enable_timer_wakeup(secs * 1000000);
		}

		esp_deep_sleep_start();
	}

	void powerOff() override {
		enterDeepSleep(0);
	}

	const char* getManufacturerName() const override {
		return "ESP32 Supermini";
	}
};