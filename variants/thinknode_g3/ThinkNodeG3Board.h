#pragma once

#include <helpers/ESP32Board.h>

// G3 LoRa LED on GPIO 6 is active-low. ESP32Board drives the TX LED
// high for on, which leaves this board's LED lit at idle.
class ThinkNodeG3Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();
#ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, HIGH); // idle = off
#endif
  }

#ifdef P_LORA_TX_LED
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);
  }

  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);
  }
#endif
};
