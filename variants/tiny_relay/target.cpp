#include "target.h"
#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

TinyRelayBoard board;

RADIO_CLASS radio = new STM32WLx_Module();

WRAPPER_CLASS radio_driver(radio, board);

static const uint32_t rfswitch_pins[] = {LORAWAN_RFSWITCH_PINS, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
    {STM32WLx::MODE_IDLE, {LOW, LOW}},
    {STM32WLx::MODE_RX, {HIGH, LOW}},
    {STM32WLx::MODE_TX_LP, {LOW, HIGH}},
    {STM32WLx::MODE_TX_HP, {LOW, HIGH}},
    END_OF_MODE_TABLE,
};

VolatileRTCClock rtc_clock;
SensorManager sensors;

#ifndef LORA_CR
#define LORA_CR 5
#endif

#ifndef STM32WL_TCXO_VOLTAGE
// TCXO set to 0 for RAK3172
#define STM32WL_TCXO_VOLTAGE 0
#endif

#ifndef LORA_TX_POWER
#define LORA_TX_POWER 22
#endif

bool radio_init()
{
    //  rtc_clock.begin(Wire);

    radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);

    int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16,
                             STM32WL_TCXO_VOLTAGE, 0);

    if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false; // fail
    }

#ifdef RX_BOOSTED_GAIN
    radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif

    radio.setCRC(1);

    return true; // success
}

uint32_t radio_get_rng_seed()
{
    return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr)
{
    radio.setFrequency(freq);
    radio.setSpreadingFactor(sf);
    radio.setBandwidth(bw);
    radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm)
{
    radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity()
{
    RadioNoiseListener rng(radio);
    return mesh::LocalIdentity(&rng); // create new random identity
}
