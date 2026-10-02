#include <Arduino.h>
#include "ThinkNodeM3Board.h"
#include <Wire.h>

#include <bluefruit.h>

void ThinkNodeM3Board::begin() {
  NRF52Board::begin();
  btn_prev_state = HIGH;

  Wire.begin();

  delay(10);   // give sx1262 some time to power up
}

void ThinkNodeM3Board::shutdownPeripherals() {
  NRF52Board::shutdownPeripherals();   // display, LoRa radio and GNSS receiver

  // GPIO output latches survive SYSTEMOFF, so every switched rail still enabled
  // here keeps draining the battery while the board looks powered off
  digitalWrite(PIN_GPS_POWER, !GPS_POWER_ACTIVE);
  digitalWrite(LED_POWER, LOW);     // RGB led supply
  digitalWrite(EEPROM_POWER, LOW);
  digitalWrite(BAT_POWER, LOW);     // battery voltage divider
  digitalWrite(PIN_EN1, LOW);
  digitalWrite(PIN_EN2, LOW);
  digitalWrite(PIN_PWR_EN, LOW);
}

void ThinkNodeM3Board::powerOff() {
  // turn off all leds, sd_power_system_off will not do this for us
  digitalWrite(PIN_LED_BLUE, !LED_STATE_ON);
  digitalWrite(PIN_LED_GREEN, !LED_STATE_ON);
  digitalWrite(PIN_LED_RED, !LED_STATE_ON);

  // the button is what powers the board back on, and SENSE is level triggered:
  // wait for the release, or the board wakes up as soon as it goes SYSTEMOFF
  uint32_t started_at = millis();
  while (digitalRead(BUTTON_PIN) == LOW) {
    if (millis() - started_at > 10000) {   // button held down for too long, or stuck:
      reboot();                            // SYSTEMOFF would wake up right away anyway
    }
    delay(10);
  }
  delay(50);   // debounce the release
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[BUTTON_PIN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

  // power off board
  NRF52Board::powerOff();
}

uint16_t ThinkNodeM3Board::getBattMilliVolts() {
  int adcvalue = 0;

  analogReference(AR_INTERNAL_2_4);
  analogReadResolution(ADC_RESOLUTION);
  delay(10);

  // ADC range is 0..2400mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * ADC_FACTOR);
}
