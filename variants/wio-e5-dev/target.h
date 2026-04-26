#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/stm32/STM32Board.h>
#include <helpers/radiolib/CustomSTM32WLxWrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>

class WIOE5Board : public STM32Board {
public:
    const char* getManufacturerName() const override {
        return "Seeed Wio E5 Dev Board";
    }

    // Just returns ADC value for now to test adc
    uint16_t getBattMilliVolts() override {
        uint32_t raw = analogRead(PIN_A3);
        return raw;
    }
};

extern WIOE5Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
