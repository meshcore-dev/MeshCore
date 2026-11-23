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

void ThinkNodeM6Board::begin() {
  NRF52Board::begin();

  digitalWrite(PIN_LED_RED, HIGH);   // power-on sign, see loop()

  startWatchdog();   // first, so that a hang later in the boot is caught too
  recoverI2cBus();
  Wire.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

  pinMode(EXT_PWR_DETECT, INPUT);
  pinMode(EXT_CHRG_DETECT, INPUT);
  pinMode(PIN_USER_BTN, INPUT_PULLUP);

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
