#include "LoRaFEMControl.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

void LoRaFEMControl::init(void) {
  pinMode(P_LORA_PA_POWER, OUTPUT);
  digitalWrite(P_LORA_PA_POWER, HIGH);
  rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason != ESP_RST_DEEPSLEEP) {
    delay(1);
  }

  rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
  rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CTX);

  pinMode(P_LORA_KCT8103L_PA_CSD, OUTPUT);
  digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
  pinMode(P_LORA_KCT8103L_PA_CTX, OUTPUT);
  digitalWrite(P_LORA_KCT8103L_PA_CTX, lna_enabled ? LOW : HIGH);
}

void LoRaFEMControl::setSleepModeEnable(void) {
  digitalWrite(P_LORA_KCT8103L_PA_CSD, LOW);
}

void LoRaFEMControl::setTxModeEnable(void) {
  digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
  digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
}

void LoRaFEMControl::setRxModeEnable(void) {
  digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
  digitalWrite(P_LORA_KCT8103L_PA_CTX, lna_enabled ? LOW : HIGH);
}

void LoRaFEMControl::setRxModeEnableWhenMCUSleep(void) {
  digitalWrite(P_LORA_PA_POWER, HIGH);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);

  digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
  digitalWrite(P_LORA_KCT8103L_PA_CTX, lna_enabled ? LOW : HIGH);
  rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
}

void LoRaFEMControl::setLNAEnable(bool enabled) {
  lna_enabled = enabled;
}
