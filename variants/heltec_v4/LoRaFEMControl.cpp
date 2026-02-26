#include "LoRaFEMControl.h"
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <Arduino.h>

void LoRaFEMControl::init(void)
{
    setLnaCanControl(false);// Default is uncontrollable 
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_GC1109_PA_EN);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_GC1109_PA_TX_EN);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CTX);

    pinMode(P_LORA_PA_POWER,OUTPUT);
    digitalWrite(P_LORA_PA_POWER,HIGH);
    delay(1);
    pinMode(P_LORA_KCT8103L_PA_CSD,INPUT); // detect which FEM is used
    delay(1);
    if(digitalRead(P_LORA_KCT8103L_PA_CSD)==HIGH) {
        // FEM is KCT8103L
        fem_type= KCT8103L_PA;
        pinMode(P_LORA_KCT8103L_PA_CSD, OUTPUT);
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        pinMode(P_LORA_KCT8103L_PA_CTX, OUTPUT);
        digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
        setLnaCanControl(true);
    } else if(digitalRead(P_LORA_KCT8103L_PA_CSD)==LOW) {
        // FEM is GC1109
        fem_type= GC1109_PA;
        pinMode(P_LORA_GC1109_PA_EN, OUTPUT);
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);
        pinMode(P_LORA_GC1109_PA_TX_EN, OUTPUT);
        digitalWrite(P_LORA_GC1109_PA_TX_EN, LOW);
    } else {
        fem_type= OTHER_FEM_TYPES;
    }
}

void LoRaFEMControl::setSleepModeEnable(void)
{
    if(fem_type==GC1109_PA) {
    /*
     * Do not switch the power on and off frequently.
     * After turning off P_LORA_PA_EN, the power consumption has dropped to the uA level.
     */
    digitalWrite(P_LORA_GC1109_PA_EN, LOW);
    digitalWrite(P_LORA_GC1109_PA_TX_EN, LOW);
    } else if(fem_type==KCT8103L_PA) {
        // shutdown the PA
        digitalWrite(P_LORA_KCT8103L_PA_CSD, LOW);
    }
}

void LoRaFEMControl::setTxModeEnable(void)
{
    if(fem_type==GC1109_PA) {
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);   // CSD=1: Chip enabled
        digitalWrite(P_LORA_GC1109_PA_TX_EN, HIGH); // CPS: 1=full PA, 0=bypass (for RX, CPS is don't care)
    } else if(fem_type==KCT8103L_PA) {
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
    }
}

void LoRaFEMControl::setRxModeEnable(void)
{
    if(fem_type==GC1109_PA) {
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);  // CSD=1: Chip enabled
        digitalWrite(P_LORA_GC1109_PA_TX_EN, LOW); 
    } else if(fem_type==KCT8103L_PA) {
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        if(lna_enabled) {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);
        } else {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
        }
    }
}

void LoRaFEMControl::setRxModeEnableWhenMCUSleep(void)
{
    digitalWrite(P_LORA_PA_POWER, HIGH);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_PA_POWER);
    if(fem_type==GC1109_PA) {
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);
        rtc_gpio_hold_en((gpio_num_t)P_LORA_GC1109_PA_EN);
        gpio_pulldown_en((gpio_num_t)P_LORA_GC1109_PA_TX_EN);
    } else if(fem_type==KCT8103L_PA) {
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
        if(lna_enabled) {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);
        } else {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);
        }
        rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
    }
}

void LoRaFEMControl::setLNAEnable(bool enabled)
{
    lna_enabled = enabled;
}
