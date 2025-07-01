#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/RadioLibWrappers.h>
#include <helpers/stm32/STM32Board.h>
#include <helpers/CustomSTM32WLxWrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>

#define  PIN_VBAT_READ    A0
#define  ADC_MULTIPLIER   (5 * 1.73 * 1000)

class RAK3x72Board : public STM32Board {
public:
    const char* getManufacturerName() const override {
        return "RAK 3x72";
    }

    uint16_t getBattMilliVolts() override {
        uint32_t raw = analogRead(PIN_VBAT_READ);            
        return (ADC_MULTIPLIER * raw) / 1024;
    }
};

extern RAK3x72Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
