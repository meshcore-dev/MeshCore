#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER_COMP ADC_MULTIPLIER          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     BATTERY_PIN
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

// Status LED (PIN_LED_RED, the enclosure's power LED) heartbeat timings.
// The M6 is a sealed outdoor box: a slow blink is the only way to tell a live
// node from a dead one, and to see whether the GNSS has a fix, without opening
// it up or attaching a laptop. Duty cycle is ~1% so it costs nothing on solar.
#define STATUS_LED_PERIOD_MS  5000   // one heartbeat every 5s
#define STATUS_LED_ON_MS        40   // length of each blink
#define STATUS_LED_GAP_MS      160   // dark gap between blinks of a double-blink

class ThinkNodeM6Board : public NRF52BoardDCDC {
  bool _booting = true;
  unsigned long _status_cycle_start = 0;
  uint8_t _status_blinks = 1;

protected:
#if NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  ThinkNodeM6Board() : NRF52Board("THINKNODE_M6_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  // Boot indicator: red LED stays solid from begin() until the sketch reports
  // that setup() finished, so a boot loop is visible as a flickering LED.
  void onBootComplete() override;

  // Drives the red LED heartbeat. Called every iteration from the variant's
  // sensor manager, which is the one place that knows the live GNSS state:
  //   1 blink  / 5s  -> running, no GPS fix (yet)
  //   2 blinks / 5s  -> running, GPS fix acquired
  void updateStatusLed(bool gps_fix);

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode M6";
  }

  void powerOff() override {

    // turn off all leds, sd_power_system_off will not do this for us
    #ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, LOW);
    #endif
    digitalWrite(PIN_LED_RED, LOW);

    // power off board
    NRF52Board::powerOff();
  }
};
