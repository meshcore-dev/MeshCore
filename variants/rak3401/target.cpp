#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include "OnboardRadioSleep.h"
#include <nrf_gpio.h>

RAK3401Board board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);

  #if defined(PIN_USER_BTN_ANA)
  MomentaryButton analog_btn(PIN_USER_BTN_ANA, 1000, 20);
  #endif
#endif

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

namespace {
class OnboardRadioIO {
public:
  void begin() {
    digitalWrite(ONBOARD_LORA_NSS, HIGH);
    pinMode(ONBOARD_LORA_NSS, OUTPUT);
    digitalWrite(ONBOARD_LORA_RESET, HIGH);
    pinMode(ONBOARD_LORA_RESET, OUTPUT);
    digitalWrite(ONBOARD_LORA_ANT_SW, LOW);
    pinMode(ONBOARD_LORA_ANT_SW, OUTPUT);
    // BUSY is weakly pulled high during startup; never override it with a pull-down.
    pinMode(ONBOARD_LORA_BUSY, INPUT_PULLUP);
    SPI.setPins(ONBOARD_LORA_MISO, ONBOARD_LORA_SCLK, ONBOARD_LORA_MOSI);
    SPI.begin();
    pinMode(ONBOARD_LORA_MISO, INPUT_PULLDOWN);
  }

  void reset() {
    digitalWrite(ONBOARD_LORA_RESET, LOW);
    delay(1);
    digitalWrite(ONBOARD_LORA_RESET, HIGH);
    delay(5);
  }

  bool waitBusy(bool high, uint32_t timeout) {
    uint32_t start = millis();
    while ((digitalRead(ONBOARD_LORA_BUSY) != LOW) != high) {
      if (millis() - start >= timeout) return false;
      delay(1);
    }
    return true;
  }

  void readVersion(char* version) {
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(ONBOARD_LORA_NSS, LOW);
    SPI.transfer(RADIOLIB_SX126X_CMD_READ_REGISTER);
    SPI.transfer(RADIOLIB_SX126X_REG_VERSION_STRING >> 8);
    SPI.transfer(RADIOLIB_SX126X_REG_VERSION_STRING & 0xFF);
    SPI.transfer(0);
    for (unsigned i = 0; i < 6; i++) version[i] = SPI.transfer(0);
    digitalWrite(ONBOARD_LORA_NSS, HIGH);
    SPI.endTransaction();
  }

  void releaseProbePulls() {
    pinMode(ONBOARD_LORA_BUSY, INPUT);
    pinMode(ONBOARD_LORA_MISO, INPUT);
  }

  void sleep() {
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(ONBOARD_LORA_NSS, LOW);
    SPI.transfer(RADIOLIB_SX126X_CMD_SET_SLEEP);
    SPI.transfer(RADIOLIB_SX126X_SLEEP_START_COLD | RADIOLIB_SX126X_SLEEP_RTC_OFF);
    digitalWrite(ONBOARD_LORA_NSS, HIGH);
    SPI.endTransaction();
    delay(1);
  }

  void end() {
    SPI.end();
    // INPUT leaves buffers enabled on floating pins; restore Nordic's disconnected default.
    nrf_gpio_cfg_default(g_ADigitalPinMap[ONBOARD_LORA_SCLK]);
    nrf_gpio_cfg_default(g_ADigitalPinMap[ONBOARD_LORA_MOSI]);
    nrf_gpio_cfg_default(g_ADigitalPinMap[ONBOARD_LORA_MISO]);
    nrf_gpio_cfg_default(g_ADigitalPinMap[ONBOARD_LORA_BUSY]);
    // An NSS falling edge wakes the radio, so leave it driven high.
    digitalWrite(ONBOARD_LORA_NSS, HIGH);
  }
};
}

rak3401::OnboardRadioState rak3401::sleepOnboardRadio() {
  OnboardRadioIO onboard;
  const uint32_t started = millis();
  // The SX1262 version register shares the "SX1261" prefix used by RadioLib.
  const auto state = rak3401::sleepOnboardRadio(onboard, RADIOLIB_SX1262_CHIP_TYPE);
  const char* outcome = state == rak3401::OnboardRadioState::Absent ? "absent" :
                        state == rak3401::OnboardRadioState::Asleep ? "asleep" :
                        state == rak3401::OnboardRadioState::BusyTimeout ? "busy-timeout" : "sleep-failed";
  MESH_DEBUG_PRINTLN("onboard SX1262: %s elapsed=%lu ms", outcome,
                    (unsigned long)(millis() - started));
  return state;
}

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

