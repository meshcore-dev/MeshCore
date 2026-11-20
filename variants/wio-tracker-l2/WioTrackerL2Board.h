#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "helpers/ESP32Board.h"

// ---------------------------------------------------------------------------
// Seeed Wio Tracker L2 Pro  (Wio-S3 module: ESP32-S3 + SX1262)
//
// Nearly all peripheral power rails and reset lines are behind a TCA9535
// 16-bit I2C IO expander at address 0x21.  Nothing (GNSS, LCD, touch, SD,
// battery ADC, speaker amp) works until the expander is configured, so
// begin() must run before any display / GPS / sensor init.
//
// Battery voltage is read through an ADS1115 16-bit I2C ADC at 0x48,
// channel AIN0, behind a x2 resistive divider (rail gated by expander).
// ---------------------------------------------------------------------------

#define TCA9535_ADDR   0x21
#define ADS1115_ADDR   0x48

// TCA9535 pin numbering: 0..7 = port 0 (P00..P07), 8..15 = port 1 (P10..P17)
#define EXP_PIN_WAKE_BTN     0   // input  - side WAKE button
#define EXP_PIN_I2C_IRQ      1   // input  - shared I2C IRQ
#define EXP_PIN_SD_DETECT    2   // input  - microSD card detect
#define EXP_PIN_TP_INT       3   // output - touch panel interrupt (driven for reset seq)
#define EXP_PIN_LCD_CS       4   // output - LCD chip select (idle high)
#define EXP_PIN_LCD_EN       5   // output - LCD power enable
#define EXP_PIN_LCD_RST      6   // output - LCD reset
#define EXP_PIN_GROVE_EN     7   // output - Grove port power
#define EXP_PIN_TP_RST       8   // output - touch panel reset
#define EXP_PIN_GNSS_RST     9   // output - GNSS reset (active HIGH)
#define EXP_PIN_USER_LED    10   // output - mesh/user LED (also GNSS wakeup)
#define EXP_PIN_OTG_EN      11   // output - USB OTG power
#define EXP_PIN_PA_EN       12   // output - speaker amp power
#define EXP_PIN_GNSS_EN     13   // output - GNSS power
#define EXP_PIN_TF_EN       14   // output - microSD power
#define EXP_PIN_BAT_ADC_EN  15   // output - battery ADC divider enable

class WioTrackerL2Board : public ESP32Board {
public:
  void begin();

  uint16_t getBattMilliVolts() override;

  const char* getManufacturerName() const override {
    return "Seeed Wio Tracker L2 Pro";
  }

  void onBeforeTransmit() override { setLed(true); }
  void onAfterTransmit() override { setLed(false); }

  // Mesh/user LED lives on the IO expander, not a GPIO
  void setLed(bool on);

  // speaker amplifier power (expander P12); off when idle to keep the GNSS
  // antenna away from class-D switching noise and to save battery
  void setSpeakerAmp(bool on) { expWritePin(EXP_PIN_PA_EN, on); }

  // hardware reset pulse to the L76K GNSS (active HIGH on this board); the
  // module keeps almanac/ephemeris across it, so this is a warm restart
  // GNSS rail: off saves the whole receiver when GPS is disabled; on re-runs
  // the power-up reset so the module comes back cleanly
  void setGnssPower(bool on) {
    if (on) {
      expWritePin(EXP_PIN_GNSS_EN, HIGH);
      delay(10);
      gnssReset();
    } else {
      expWritePin(EXP_PIN_GNSS_EN, LOW);
    }
  }
  // Grove expansion port rail (nothing on-board depends on it)
  void setGrovePower(bool on) { expWritePin(EXP_PIN_GROVE_EN, on); }

  void gnssReset() {
    expWritePin(EXP_PIN_GNSS_RST, HIGH);
    delay(10);
    expWritePin(EXP_PIN_GNSS_RST, LOW);
  }

  // WAKE button on expander P00: pressed = level differs from boot baseline
  bool readWakeButton();

  // VBUS presence via the AW35615 USB-C controller (I2C 0x22)
  bool isExternalPowered() override;

  bool expanderOK() const { return expander_ok; }

private:
  uint8_t out_shadow[2] = { 0xFF, 0xFF };  // TCA9535 output regs default high
  uint8_t cfg_shadow[2] = { 0xFF, 0xFF };  // 1 = input (power-on default)
  bool expander_ok = false;
  bool aw_ok = false;              // AW35615 USB-C controller responded at probe
  uint8_t wake_btn_baseline = 0;   // idle level of P00, captured at init

  int expReadInputs();   // 16-bit input register pair, -1 on error

  bool expWriteReg(uint8_t reg, uint8_t val);
  void expSetOutput(uint8_t pin, bool initial_level);
  void expSetInput(uint8_t pin);
  void expWritePin(uint8_t pin, bool level);
  bool initExpander();

  int16_t adsReadRaw();
};
