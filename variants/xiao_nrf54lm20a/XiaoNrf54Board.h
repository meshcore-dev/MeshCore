#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/nrf54/NRF54Board.h>
#include <npm1300.h>

class XiaoNrf54Board : public NRF54Board {
public:
  XiaoNrf54Board() : NRF54Board("XIAO_NRF54_OTA") { }

  void begin() override {
    NRF54Board::begin();

    // nPM1300 charger is off at power-up, so configure it to match Seeed's Zephyr defaults:
    // 10k NTC, 4.2V termination (4.0V when warm), 150mA charge current.
    // The settings survive a reset, so disable charging while changing them.
    npm1300_charger_enable(false);
    bool ok = npm1300_write_reg(NPM1300_BASE_ADC, 0x0A, 1) &&      // ADCNTCRSEL: 10k NTC
              npm1300_charger_set_term_voltage(4200) &&
              npm1300_write_reg(NPM1300_BASE_CHARGER, 0x0D, 4) &&  // BCHGVTERMR: 4.0V when warm
              npm1300_charger_set_current(150);
    // always turn charging back on, even if a setting failed
    if (!npm1300_charger_enable(true) || !ok) {
      MESH_DEBUG_PRINTLN("nPM1300: failed to set up the charger");
    }

    // Raise the USB input current limit last, since setting the charge current lowers it.
    // The default 100mA limit isn't enough for a 22dBm TX and browns out the board.
    if (!npm1300_vbus_set_input_current_limit_ma(500)) {
      MESH_DEBUG_PRINTLN("nPM1300: failed to raise the USB input current limit");
    }

#if defined(P_LORA_TX_LED)
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, HIGH);
#endif
  }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    int32_t mv = npm1300_read_vbat_mv();
    return mv > 0 ? mv : 0;
  }

  bool hasUsbPowerDetect() const override { return true; }

  bool isUsbPowered() override {
    uint8_t status;
    return npm1300_vbus_status(&status) && (status & NPM1300_VBUS_STATUS_PRESENT);
  }

  bool isExternalPowered() override { return isUsbPowered(); }

  bool isChargerActive() override { return npm1300_charger_is_charging(); }

  const char* getManufacturerName() const override {
    return "Seeed Xiao-nrf54lm20a";
  }
};
