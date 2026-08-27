#include "LoRaFEMControl.h"

#include <Arduino.h>
#include "variant.h"

static void enableFEMPower() {
  bool wasOff = digitalRead(LORA_KCT8103L_EN) != HIGH;
  digitalWrite(LORA_KCT8103L_EN, HIGH);
  if (wasOff) {
    delay(5);
  }
}

void LoRaFEMControl::init() {
  pinMode(LORA_KCT8103L_EN, OUTPUT);
  digitalWrite(LORA_KCT8103L_EN, HIGH);
  delay(1);
  pinMode(LORA_KCT8103L_TX_RX, OUTPUT);
  digitalWrite(LORA_KCT8103L_TX_RX, LOW);
}

void LoRaFEMControl::setSleepModeEnable() {
  pinMode(LORA_KCT8103L_EN, OUTPUT);
  digitalWrite(LORA_KCT8103L_EN, LOW);
}

void LoRaFEMControl::setTxModeEnable() {
  enableFEMPower();
  digitalWrite(LORA_KCT8103L_TX_RX, HIGH);
}

void LoRaFEMControl::setRxModeEnable() {
  enableFEMPower();
  digitalWrite(LORA_KCT8103L_TX_RX, LOW);
}

void LoRaFEMControl::setRxModeEnableWhenMCUSleep() {
  enableFEMPower();
  digitalWrite(LORA_KCT8103L_TX_RX, LOW);
}
