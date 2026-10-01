#include "LoRaFEMControl.h"
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <Arduino.h>

void LoRaFEMControl::init(void)
{
    // Power on FEM LDO — set registers before releasing RTC hold for
    // atomic transition (no glitch on deep sleep wake).
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, HIGH);
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_PA_POWER);

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_DEEPSLEEP) {
        delay(1);  // FEM startup time after cold power-on
    }

    // Auto-detect FEM type via shared GPIO2 default pull level.
    // GC1109 CSD: internal pull-down → reads LOW
    // KCT8103L CSD: internal pull-up → reads HIGH
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CSD);
    pinMode(P_LORA_KCT8103L_PA_CSD, INPUT);
    delay(1);
    if(digitalRead(P_LORA_KCT8103L_PA_CSD)==HIGH) {
        // FEM is KCT8103L (V4.3)
        fem_type= KCT8103L_PA;
        pinMode(P_LORA_KCT8103L_PA_CSD, OUTPUT);
        digitalWrite(P_LORA_KCT8103L_PA_CSD, HIGH);
        rtc_gpio_hold_dis((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
        pinMode(P_LORA_KCT8103L_PA_CTX, OUTPUT);
        digitalWrite(P_LORA_KCT8103L_PA_CTX, lna_enabled ? LOW : HIGH);
        setLnaCanControl(true);
    } else {
        // FEM is GC1109 (V4.2)
        fem_type= GC1109_PA;
        pinMode(P_LORA_GC1109_PA_EN, OUTPUT);
        digitalWrite(P_LORA_GC1109_PA_EN, HIGH);
        pinMode(P_LORA_GC1109_PA_TX_EN, OUTPUT);
        digitalWrite(P_LORA_GC1109_PA_TX_EN, LOW);
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
            digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);   // LNA on
        } else {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);  // LNA bypass
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
            digitalWrite(P_LORA_KCT8103L_PA_CTX, LOW);   // LNA on
        } else {
            digitalWrite(P_LORA_KCT8103L_PA_CTX, HIGH);  // LNA bypass
        }
        rtc_gpio_hold_en((gpio_num_t)P_LORA_KCT8103L_PA_CTX);
    }
}

void LoRaFEMControl::setLNAEnable(bool enabled)
{
    lna_enabled = enabled;
}
