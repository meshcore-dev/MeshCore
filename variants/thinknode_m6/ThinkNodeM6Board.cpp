#include "ThinkNodeM6Board.h"
#include "StatusLed.h"
#include <Arduino.h>
#include <target.h>

#ifdef THINKNODE_M6

#include <Wire.h>

// A reset in the middle of an RTC read can leave the RTC holding SDA low,
// and the RTC probe at boot then hangs forever. Up to nine clock pulses let
// it finish its byte and release the bus.
static void recoverI2cBus() {
  pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
  pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
  MESH_DEBUG_PRINTLN("I2C at boot: SDA=%d SCL=%d", digitalRead(PIN_WIRE_SDA), digitalRead(PIN_WIRE_SCL));
  pinMode(PIN_WIRE_SCL, OUTPUT);
  digitalWrite(PIN_WIRE_SCL, HIGH);
  for (int i = 0; i < 9 && digitalRead(PIN_WIRE_SDA) == LOW; i++) {
    digitalWrite(PIN_WIRE_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_WIRE_SCL, HIGH);
    delayMicroseconds(5);
  }
  pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
}

// Resets the board when loop() stops feeding it, for example on an I2C read
// that never ends. Like Meshtastic: 90 s, paused while the CPU sleeps or a
// debugger halts it. A pin reset stops it, a soft reset does not.
static void startWatchdog() {
  if (NRF_WDT->RUNSTATUS) {
    return;   // still running after a soft reset, and locked until the next one
  }
  NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Pause << WDT_CONFIG_SLEEP_Pos)
                    | (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);
  NRF_WDT->CRV = M6_WATCHDOG_SECS * 32768;   // counts the 32.768 kHz clock
  NRF_WDT->RREN = WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
}

// index 0 is GPREGRET, read by the bootloader; index 1 is GPREGRET2
static void writeGpregret(uint8_t index, uint8_t value) {
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_power_gpregret_clr(index, 0xFF);
    sd_power_gpregret_set(index, value);
  } else if (index == 0) {
    NRF_POWER->GPREGRET = value;
  } else {
    NRF_POWER->GPREGRET2 = value;
  }
}

// Turns the LEDs off and sets what the next wake-up needs. Like Meshtastic,
// the button or the charger starting wakes the board; if the charger is
// already active, it wakes again at once.
static void prepareWakeUp() {
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_BLUE, LOW);
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_USER_BTN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[EXT_CHRG_DETECT], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);

  // The bootloader enters its update mode (blue LED pulsing) when the button
  // is held at boot, and the button is what wakes the board: ask it to skip
  // that check, as Meshtastic does.
  writeGpregret(0, M6_DFU_MAGIC_SKIP);
  writeGpregret(1, M6_POWEROFF_MARK);   // see checkPowerOnPress()
}

void ThinkNodeM6Board::powerOff() {
  // Elecrow: the red LED flashes 5 times before power off
  for (int i = 0; i < 5; i++) {
    digitalWrite(PIN_LED_RED, HIGH);
    delay(250);
    digitalWrite(PIN_LED_RED, LOW);
    delay(250);
  }
  prepareWakeUp();
  NRF52Board::powerOff();
}

// Elecrow manual: after a power off, the button must be held 3 s to power
// on, and the red LED then lights. A shorter press puts the board back to
// sleep. A wake-up by the charger boots at once.
void ThinkNodeM6Board::checkPowerOnPress() {
  bool after_power_off = NRF_POWER->GPREGRET2 == M6_POWEROFF_MARK;   // SoftDevice not started yet
  writeGpregret(1, 0);
  if (!after_power_off || isChargerActive()) {
    return;
  }
  while (millis() < M6_POWERON_HOLD_MILLIS) {
    if (digitalRead(PIN_USER_BTN) == HIGH) {
      delay(20);
      if (digitalRead(PIN_USER_BTN) == HIGH) {   // still released, not a bounce
        // initVariant() powered the GPS on again, and the reset may have woken
        // the radio: the normal power-off path puts the radio back to sleep.
        digitalWrite(PIN_GPS_EN, LOW);
        prepareWakeUp();
        NRF52Board::powerOff();
      }
    }
    delay(10);
  }
}

void ThinkNodeM6Board::begin() {
  NRF52Board::begin();

  pinMode(EXT_PWR_DETECT, INPUT);
  pinMode(EXT_CHRG_DETECT, INPUT);
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  startWatchdog();   // first, so that a hang later in the boot is caught too
  checkPowerOnPress();

  digitalWrite(PIN_LED_RED, HIGH);   // power-on sign, see loop()

  recoverI2cBus();
  Wire.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

  delay(10); // give sx1262 some time to power up
}

void ThinkNodeM6Board::loop() {
  uint32_t now = millis();

  NRF_WDT->RR[0] = WDT_RR_RR_Reload;   // feed the watchdog

  // Like Meshtastic, power off when the button is released after a long hold.
  // Presses that start during boot are ignored: a floating pin could fake one.
  if (digitalRead(PIN_USER_BTN) == LOW) {
    if (_btn_down_at == 0) {
      _btn_down_at = now;
    }
  } else {
    bool long_press = _btn_down_at > M6_BTN_BOOT_HOLDOFF_MILLIS
                      && now - _btn_down_at >= M6_POWEROFF_HOLD_MILLIS;
    if (long_press) {
      powerOff();  // does not return
    }
    _btn_down_at = 0;
  }

  if (_batt_read_at == 0 || now - _batt_read_at >= M6_BATT_READ_MILLIS) {
    _batt_mv = getBattMilliVolts();
    _batt_read_at = now;
  }
  bool red_on = m6RedLed(now, isChargerActive(), _batt_mv);
  if (red_on != _red_led_on) {
    digitalWrite(PIN_LED_RED, red_on ? HIGH : LOW);
    _red_led_on = red_on;
  }

#ifdef P_LORA_TX_LED
  uint32_t rx_count = radio_driver.getPacketsRecv();
  if (rx_count != _rx_count) {
    _rx_count = rx_count;
    _rx_at = now;
    _data_started_at = now;
  }
  bool data_active = _tx_active || now - _rx_at < M6_RX_FLASH_MILLIS;
  bool blue_on = m6BlueLed(data_active, now - _data_started_at);
  if (blue_on != _blue_led_on) {
    digitalWrite(P_LORA_TX_LED, blue_on ? HIGH : LOW);
    _blue_led_on = blue_on;
  }
#endif
}

uint16_t ThinkNodeM6Board::getBattMilliVolts() {
  int adcvalue = 0;

  digitalWrite(PIN_ADC_CTRL, HIGH);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(10);

  // ADC range is 0..3000mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  digitalWrite(PIN_ADC_CTRL, LOW);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * REAL_VBAT_MV_PER_LSB);
}
#endif
