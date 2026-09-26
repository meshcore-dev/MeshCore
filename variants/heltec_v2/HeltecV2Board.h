#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

// built-ins
#define  PIN_VBAT_READ   37
#define  PIN_LED_BUILTIN 25

// Battery measurement (Heltec WiFi LoRa 32 V2 / V2.1)
//  GPIO21 drives the enable of BOTH the VEXT rail and the battery voltage
//  divider (ACTIVE-LOW: LOW = ON). Without this the divider output floats up
//  to ~VBAT and the ADC saturates. preDeepSleep() drives it back HIGH
//  before deep sleep (power saving); begin() re-enables it on every wake.
//  The divider (R10/R12) has DIFFERENT values by hardware revision:
//      original V2 (2018): GPIO13 (ADC2), 220K/100K = 3.2:1 (per schematic)
//                           - readable only while WiFi is off
//      V2.1 (2019+):       GPIO37 (ADC1), 270K/100K = 3.7:1 (measured:
//                           VBAT 4.23 V -> raw 356 @ 10 bit => 3.687; the
//                           270K/100K nominal of 3.7 matches within the
//                           2 % resistor tolerance) - always readable
//  ->  VBAT = (raw/1024 * 3.3V) * ratio(revision).
//  begin() probes both pins once, remembers the detected revision, and from
//  then on only the detected channel is attached and sampled.
#define  PIN_VEXT_ENABLE 21
#define  PIN_VBAT_V2     13
#define  PIN_VBAT_V2_1   37

class HeltecV2Board : public ESP32Board {
public:
  enum HwRev { HW_REV_V2 = 0, HW_REV_V2_1 = 1, HW_REV_UNKNOWN = 2 };

  // Divider ratio (R10+R12)/R12 per revision, see the block comment above.
  static float vbatRatio(HwRev rev) {
    return (rev == HW_REV_V2) ? 3.2f : 3.7f;
  }

  void begin() {
    ESP32Board::begin();

    // Enable the battery voltage divider (and the VEXT rail). GPIO21 is
    // active-LOW on both V2 and V2.1: LOW = divider ON.
    pinMode(PIN_VEXT_ENABLE, OUTPUT);
    digitalWrite(PIN_VEXT_ENABLE, LOW);

    analogReadResolution(10);
    delay(100);  // let the divider settle after being enabled

    // Detect the hardware revision by probing both candidate pins.
    // No adcAttachPin() needed for the probe: every ADC channel already has
    // its reset-default attenuation (11 dB) at boot, which is what the
    // formula below assumes.
    const uint32_t raw37 = vbatAvg10(PIN_VBAT_V2_1);
    const uint32_t raw13 = vbatAvg10(PIN_VBAT_V2);
    // Each candidate pin is scored with ITS OWN revision's divider ratio:
    const uint16_t mv37  = vbatMV(raw37, vbatRatio(HW_REV_V2_1));
    const uint16_t mv13  = vbatMV(raw13, vbatRatio(HW_REV_V2));

    // A Li-ion pack at the divider input reads ~2300..4500 mV.
    const bool ok37 = (mv37 >= 2300) && (mv37 <= 4500);
    const bool ok13 = (mv13 >= 2300) && (mv13 <= 4500);

    if (ok37)               { hwRev = HW_REV_V2_1;  vbatPin = PIN_VBAT_V2_1; }
    else if (ok13)          { hwRev = HW_REV_V2;    vbatPin = PIN_VBAT_V2; }
    else                    { hwRev = HW_REV_UNKNOWN; vbatPin = PIN_VBAT_V2_1; }

    // Attach ONLY the detected channel; the other one is never attached
    // (the arduino-esp32 core has no detach API - "not attached" is the
    // detach). Note the base class still attaches PIN_VBAT_READ (37); on an
    // original V2 that pin is simply unconnected and harmless.
    adcAttachPin(vbatPin);

    // Serial.printf("[vbat] HW=%s pin=%u  raw37=%u(%umV) raw13=%u(%umV)\n",
    //               getManufacturerName(), (unsigned)vbatPin,
    //               (unsigned)raw37, mv37, (unsigned)raw13, mv13);

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_0)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_0);
    }
  }

  uint16_t getBattMilliVolts() override {
    if (hwRev == HW_REV_UNKNOWN) {
      return 0;  // revision could not be detected - report unsupported
    }
    analogReadResolution(10);
    return vbatMV(vbatAvg10(vbatPin), vbatRatio(hwRev));
  }

  // Called by the base enterDeepSleep() as the last step before
  // esp_deep_sleep_start() (see ESP32Board::preDeepSleep()). Cut the
  // VEXT + divider rail for the whole sleep. GPIO state is retained
  // across deep sleep; begin() re-enables the rail on every wake.
  void preDeepSleep() override {
    digitalWrite(PIN_VEXT_ENABLE, HIGH);
  }

  const char* getManufacturerName() const override {
    return (hwRev == HW_REV_V2_1) ? "Heltec V2.1" : "Heltec V2";
  }

  HwRev getHwRev() const { return hwRev; }

  uint32_t getIRQGpio() override {
    return P_LORA_DIO_0; // default for SX1276
  }

private:
  HwRev hwRev = HW_REV_UNKNOWN;
  uint8_t vbatPin = PIN_VBAT_V2_1;

  // Convert a 10-bit sample from the divided line to millivolts at the pack:
  //   mV = (raw/1024 * 3.3V) * ratio
  static uint16_t vbatMV(uint32_t raw10, float ratio) {
    return (uint16_t)((ratio * (3.3 / 1024.0) * (double)raw10) * 1000.0);
  }

  static uint32_t vbatAvg10(uint8_t pin) {
    uint32_t s = 0;
    for (int i = 0; i < 8; i++) s += analogRead(pin);
    return s / 8;
  }
};
