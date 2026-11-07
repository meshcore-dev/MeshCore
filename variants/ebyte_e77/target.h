#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/CustomSTM32WLxWrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/stm32/STM32Board.h>

#define PIN_VBAT_READ PIN_A0
#define ADC_MULTIPLIER (5 * 1.73 * 1000)

class EbyteE77Board : public STM32Board
{
  public:
    void begin() override
    {
        STM32Board::begin();
        pinMode(P_LORA_TX_LED, OUTPUT);
        pinMode(P_LORA_RX_LED, OUTPUT);
        pinMode(PIN_BUTTON1, INPUT_PULLUP);
        pinMode(PIN_BUTTON2, INPUT_PULLUP);
    }

    const char *getManufacturerName() const override { return "Ebyte E77"; }

    uint16_t getBattMilliVolts() override
    {
        analogReadResolution(12);
        uint32_t raw = 0;
        for (int i = 0; i < 8; i++) {
            raw += analogRead(PIN_VBAT_READ);
        }
        return ((double)raw) * ADC_MULTIPLIER / 8 / 4096;
    }

    #if defined(P_LORA_TX_LED)
    void onBeforeTransmit() override {
        digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    }
    void onAfterTransmit() override {
        digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
    }
    #endif
};

extern EbyteE77Board board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
