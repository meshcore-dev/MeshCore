#pragma once

#include <RadioLib.h>

class CustomSTM32WLx : public STM32WLx {
  public:
    CustomSTM32WLx(STM32WLx_Module *mod) : STM32WLx(mod) { }

    int16_t startReceive() override {
      // include the PREAMBLE_DETECTED irq bit in reported flags
      return STM32WLx::startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED), RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    bool isReceiving() {
      uint16_t irq = getIrqFlags();
      bool detected = (irq & RADIOLIB_SX126X_IRQ_HEADER_VALID) || (irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
      return detected;
    }
};