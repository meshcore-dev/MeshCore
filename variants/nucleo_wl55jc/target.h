#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/stm32/STM32Board.h>
#include <helpers/radiolib/CustomSTM32WLxWrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>

class NucleoWL55JCBoard : public STM32Board {
public:
    void begin() override {
        STM32Board::begin();

        pinMode(P_LORA_TX_LED, OUTPUT);
        digitalWrite(P_LORA_TX_LED, LOW);   // see note on LED polarity below
        pinMode(PIN_USER_BTN, INPUT_PULLUP);
    }

    const char* getManufacturerName() const override {
        return "ST Nucleo WL55JC1";
    }

    // LD1..LD3 on the Nucleo are driven active HIGH, whereas STM32Board's
    // default TX-LED handling assumes the active LOW wiring used by the
    // Wio-E5 boards. Invert both halves here.
    void onBeforeTransmit() override {
        digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    }
    void onAfterTransmit() override {
        digitalWrite(P_LORA_TX_LED, LOW);    // turn TX LED off
    }

    // No battery on this dev board, it is USB/ST-LINK powered.
    uint16_t getBattMilliVolts() override {
        return 0;
    }
};

extern NucleoWL55JCBoard board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
