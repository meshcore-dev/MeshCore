#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/stm32/STM32Board.h>
#include <helpers/radiolib/CustomSTM32WLxWrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#ifdef DISPLAY_CLASS
  #include "NullDisplayDriver.h"
#endif

#include <BME280I2C.h>
#include <Wire.h>

#ifdef DISPLAY_CLASS
  extern NullDisplayDriver display;
#endif

class WIOE5Board : public STM32Board {
public:
    void begin() override {
        STM32Board::begin();

        pinMode(LED_RED, OUTPUT);
        digitalWrite(LED_RED, HIGH);
        pinMode(USER_BTN, INPUT_PULLUP);
    }

    const char* getManufacturerName() const override {
        return "Seeed Wio E5 mini";
    }

    uint16_t getBattMilliVolts() override {
        analogReadResolution(12);
        uint32_t raw = 0;
        for (int i=0; i<8;i++) {
            raw += analogRead(PIN_A3);
        }
        return ((double)raw) * 1.73 * 5 * 1000 / 8 / 4096;
    }
};

class WIOE5SensorManager : public SensorManager {
    BME280I2C bme;
    bool has_bme = false;

public:
    WIOE5SensorManager() {}
    bool begin() override;
    bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
};

extern WIOE5Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern WIOE5SensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
