#include "LoRaFEMControl.h"
#include <Arduino.h>

void LoRaFEMControl::init(void)
{
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, HIGH);
    delay(1);
    pinMode(P_LORA_KCT8103L_PA_CSD, OUTPUT);
    digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
    pinMode(P_LORA_KCT8103L_PA_CTX, OUTPUT);
    digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
    setLnaCanControl(true);
}

void LoRaFEMControl::setSleepModeEnable(void)
{
    // shutdown the PA
    digitalWrite(P_LORA_KCT8103L_PA_CSD, LOW);
}

void LoRaFEMControl::setTxModeEnable(void)
{
    digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
    digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
}

void LoRaFEMControl::setRxModeEnable(void)
{
    digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
    if (lna_enabled) {
        digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);
    } else {
        digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
    }
}

void LoRaFEMControl::setRxModeEnableWhenMCUSleep(void)
{
    digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
    if (lna_enabled) {
        digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);
    } else {
        digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
    }
}

void LoRaFEMControl::setLNAEnable(bool enabled)
{
    lna_enabled = enabled;
}
