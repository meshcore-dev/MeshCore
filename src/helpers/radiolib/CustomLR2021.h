#pragma once

#include <RadioLib.h>
#include "MeshCore.h"

class CustomLR2021 : public LR2021 {
  bool _rx_boosted = false;

  public:
    CustomLR2021(Module *mod) : LR2021(mod) {
      irqDioNum = LR2021_IRQ_DIO;
    }

    float getFreqMHz() const { return freqMHz; }

    uint8_t getSpreadingFactor() const { return spreadingFactor; }

    int16_t setRxBoostedGainMode(uint8_t level) {
      _rx_boosted = (level > 0);
      return LR2021::setRxBoostedGainMode(level);
    }

    bool getRxBoostedGainMode() const { return _rx_boosted; }

    bool isReceiving() {
      uint32_t irq = getIrqStatus();
      // Use LR2021-specific IRQ flags if available, fall back to LR11x0
#ifdef RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED
      bool detected = (irq & RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID) ||
                      (irq & RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED);
#else
      bool detected = (irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID) ||
                      (irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
#endif
      return detected;
    }

    bool std_init(SPIClass *spi = NULL) {
      (void)spi;

      int status = begin(LORA_FREQ, LORA_BW, LORA_SF, 5,
                         RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE,
                         LORA_TX_POWER, 16, 0.0);
      if (status != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("LR2021: radio init failed: %d", status);
        return false;
      }

      setCRC(1);
      return true;
    }
};
