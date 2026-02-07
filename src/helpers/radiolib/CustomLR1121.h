#pragma once

#include <RadioLib.h>

class CustomLR1121 : public LR1121 {
public:
  CustomLR1121(Module* mod) : LR1121(mod) { }

  size_t getPacketLength(bool update) override {
    size_t len = LR1121::getPacketLength(update);
    if (len == 0 && (getIrqStatus() & RADIOLIB_LR11X0_IRQ_HEADER_ERR)) {
      // Recover from sporadic shifted-packet state seen after header errors.
      standby();
    }
    return len;
  }

  bool isReceiving() {
    uint16_t irq = getIrqStatus();
    return (irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID) ||
           (irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
  }
};
