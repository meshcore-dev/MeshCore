#include <Arduino.h>
#include <nrf_gpio.h>
#include "target.h"
#include "variant.h"

muzi_baseBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  MuziBaseSensorManager sensors = MuziBaseSensorManager(nmea);
#else
  MuziBaseSensorManager sensors;
#endif

// user button for the N-click power-off. single-click mode (multiclick off) so
// each press is its own CLICK; we count them ourselves. reliable across the
// idle-sleep loop because it's the same MomentaryButton the UI navigates with.
static MomentaryButton pwr_btn(PIN_USER_BTN, 0, true, true, false);

// power off, arming the user button as a wake source first so a later press
// turns it back on. wait for the button to release (the 5th click already fires
// on release, so this returns quickly), then set nRF52 GPIO SENSE (press = LOW);
// it survives into SYSTEMOFF because shutdownPeripherals() doesn't touch this pin.
static void muziPowerOff() {
  uint32_t t0 = millis();
  while (digitalRead(PIN_USER_BTN) == USER_BTN_PRESSED && (millis() - t0) < 3000) { delay(5); }
  delay(50);   // settle
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_USER_BTN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  board.powerOff();   // shutdownPeripherals() + SYSTEMOFF
}

bool MuziBaseSensorManager::begin() {
  pwr_btn.begin();
  bool ok = EnvironmentSensorManager::begin();
#if ENV_INCLUDE_GPS
  pinMode(PIN_GPS_SWITCH, INPUT);
  _last_gps_sw = digitalRead(PIN_GPS_SWITCH);   // initial gps state from the switch
  if (_last_gps_sw == HIGH) start_gps(); else stop_gps();
#endif
  return ok;
}

void MuziBaseSensorManager::loop() {
  // user button: USER_BTN_POWER_OFF_CLICKS quick clicks -> power off
  if (pwr_btn.check() == BUTTON_EVENT_CLICK) {
    unsigned long t = millis();
    if (t - _pwr_last_click > 2000) _pwr_clicks = 0;   // restart if too slow
    _pwr_last_click = t;
    if (++_pwr_clicks >= USER_BTN_POWER_OFF_CLICKS) muziPowerOff();
  }
#if ENV_INCLUDE_GPS
  unsigned long now = millis();
  if (now > _next_sw_check) {   // check the mode switch ~once a sec
    _next_sw_check = now + 1000;
    int sw = digitalRead(PIN_GPS_SWITCH);
    if (sw != _last_gps_sw) {
      _last_gps_sw = sw;
      if (sw == HIGH) start_gps(); else stop_gps();
    }
  }
#endif
  EnvironmentSensorManager::loop();
}

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, false, false);
  MomentaryButton joystick_left(JOYSTICK_LEFT, 1000, true, false, false);
  MomentaryButton joystick_right(JOYSTICK_RIGHT, 1000, true, false, false);
  MomentaryButton back_btn(PIN_BACK_BTN, 1000, true, false, true);
#endif

#if defined(USE_LR1121)
  #ifndef LORA_CR
    #define LORA_CR      5
  #endif

  #ifdef RF_SWITCH_TABLE
  static const uint32_t rfswitch_dios[Module::RFSWITCH_MAX_PINS] = {
    RADIOLIB_LR11X0_DIO5,
    RADIOLIB_LR11X0_DIO6,
    RADIOLIB_NC
  };

  static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                 DIO5  DIO6
    { LR11x0::MODE_STBY,   {LOW,  LOW}},
    { LR11x0::MODE_RX,     {HIGH, LOW}},
    { LR11x0::MODE_TX,     {LOW,  HIGH}},
    { LR11x0::MODE_TX_HP,  {LOW,  HIGH}},
    { LR11x0::MODE_TX_HF,  {LOW,  LOW}},
    { LR11x0::MODE_GNSS,   {LOW,  LOW}},
    { LR11x0::MODE_WIFI,   {LOW,  LOW}},
    END_OF_MODE_TABLE,
  };
  #endif
#endif // USE_LR1121

bool radio_init() {
  //rtc_clock.begin(Wire);

#if defined(USE_LR1121)
  #ifdef LR11X0_DIO3_TCXO_VOLTAGE
    float tcxo = LR11X0_DIO3_TCXO_VOLTAGE;
  #else
    float tcxo = 1.6f;
  #endif

  SPI.setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI);
  SPI.begin();
  int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    return false;  // fail
  }

  radio.setCRC(2);
  radio.explicitHeader();

  #ifdef RF_SWITCH_TABLE
  radio.setRfSwitchTable(rfswitch_dios, rfswitch_table);
  #endif
  #ifdef RX_BOOSTED_GAIN
  radio.setRxBoostedGainMode(RX_BOOSTED_GAIN);
  #endif

  return true;  // success
#else  // USE_SX1262
  // CustomSX1262::std_init() configures the SPI pins, runs begin() with the
  // TCXO-voltage fallback, sets CRC, current limit, the DIO2 RF switch, and
  // RX boosted gain from the SX126X_* build flags.
  return radio.std_init(&SPI);
#endif
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
