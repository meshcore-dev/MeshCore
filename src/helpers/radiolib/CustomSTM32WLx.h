#pragma once

#include <RadioLib.h>

#define SX126X_IRQ_HEADER_VALID      0b0000010000 //  4     4     valid LoRa header received
#define SX126X_IRQ_PREAMBLE_DETECTED 0x04

class CustomSTM32WLx : public STM32WLx {
public:
  CustomSTM32WLx(STM32WLx_Module *mod) : STM32WLx(mod) {}

  bool isReceiving() {
    uint16_t irq = getIrqFlags();
    bool detected = (irq & SX126X_IRQ_HEADER_VALID) || (irq & SX126X_IRQ_PREAMBLE_DETECTED);
    return detected;
  }
};