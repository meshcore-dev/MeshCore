#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/KeyValueStore.h>
#include <hardware/sync.h>
#include <pico/time.h>

// iLabs Challenger RP2040 LoRa (1st generation)
// No battery-voltage divider identified on this board, so getBattMilliVolts() reports 0.

class ChallengerBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

  static int64_t onSleepAlarm(alarm_id_t, void*) { return 0; }  // wake only, don't repeat

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

  void attachDynamicPrefs(KeyValueStore* prefs) { }  // no-op

#ifdef P_LORA_TX_LED
  void onBeforeTransmit() override { digitalWrite(P_LORA_TX_LED, HIGH); }
  void onAfterTransmit() override { digitalWrite(P_LORA_TX_LED, LOW); }
#endif

  uint16_t getBattMilliVolts() override { return 0; }

  const char *getManufacturerName() const override { return "iLabs Challenger RP2040 LoRa"; }

  void reboot() override { rp2040.reboot(); }

  // Light sleep for 'powersaving on': halt the core until an interrupt
  // (LoRa DIO0, USB, or the timer alarm). Interrupts are masked around the
  // DIO0 check so a packet arriving just before WFI still wakes it.
  void sleep(uint32_t secs) override {
    alarm_id_t alarm = 0;
    if (secs > 0) {
      alarm = add_alarm_in_ms(secs * 1000, onSleepAlarm, NULL, true);
    }

    uint32_t irq_state = save_and_disable_interrupts();
    if (digitalRead(P_LORA_DIO_0) == LOW) {  // no packet waiting
      __wfi();
    }
    restore_interrupts(irq_state);

    if (alarm > 0) cancel_alarm(alarm);
  }

  bool startOTAUpdate(const char *id, char reply[]) override { return false; }
};
