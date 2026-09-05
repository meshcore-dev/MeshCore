#include "LoRaFEMControl.h"
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <Arduino.h>

void LoRaFEMControl::init(void)
{
#if defined(P_LORA_PA_POWER) && defined(P_LORA_KCT8103L_PA_CSD) && defined(P_LORA_KCT8103L_PA_CTX)
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, HIGH);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
    delay(1);
    pinMode(P_LORA_KCT8103L_PA_CSD, OUTPUT);
    digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
    pinMode(P_LORA_KCT8103L_PA_CTX, OUTPUT);
    digitalWrite(P_LORA_KCT8103L_PA_CTX, lna_enabled ? LOW : HIGH);
    setLnaCanControl(true);
#endif
}

void LoRaFEMControl::setSleepModeEnable(void)
{
#if defined(P_LORA_KCT8103L_PA_CSD)
    // shutdown the PA
    digitalWrite(P_LORA_KCT8103L_PA_CSD, LOW);
#endif
}

void LoRaFEMControl::setTxModeEnable(void)
{
#if defined(P_LORA_KCT8103L_PA_CSD) && defined(P_LORA_KCT8103L_PA_CTX)
    digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
    digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
#endif
}

void LoRaFEMControl::setRxModeEnable(void)
{
#if defined(P_LORA_KCT8103L_PA_CSD) && defined(P_LORA_KCT8103L_PA_CTX)
    digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
    if (lna_enabled) {
        digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);
    } else {
        digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
    }
#endif
}

void LoRaFEMControl::setRxModeEnableWhenMCUSleep(void)
{
#if defined(P_LORA_KCT8103L_PA_CSD) && defined(P_LORA_KCT8103L_PA_CTX)
    digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
    if (lna_enabled) {
        digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);
    } else {
        digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
    }
    rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
#endif
}

void LoRaFEMControl::setLNAEnable(bool enabled)
{
    lna_enabled = enabled;
}
