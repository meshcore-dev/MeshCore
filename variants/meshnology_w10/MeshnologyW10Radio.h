#pragma once

#include "MCP23017.h"
#include <helpers/radiolib/CustomSX1262Wrapper.h>

class MeshnologyW10Radio : public CustomSX1262Wrapper {
  static_assert(isExpanderVirtualPin(P_LORA_DIO_1), "SX1262 DIO1 is an MCP23017 line and must be given as a virtual pin");
  static constexpr uint8_t EXIO_LORA_DIO1 = expanderPin(P_LORA_DIO_1);

  MCP23017& _io;

protected:
  bool isIRQPending() const override {
    uint8_t value;
    return _io.digitalRead(EXIO_LORA_DIO1, value) && value == HIGH;
  }

  bool isRecvIRQPending() const override {
    // Qualify the polled level without changing latched IRQ handling on other boards.
    return isInRecvMode() && isIRQPending();
  }

public:
  int16_t performChannelScan() override {
    auto* radio = static_cast<CustomSX1262*>(_radio);
    int16_t result = radio->startChannelScan();
    if (result != RADIOLIB_ERR_NONE) return result;

    // RadioLib scan blocks with no DIO1 timeout -- don't wait forever on failed IO expander/radio
    constexpr uint32_t CAD_TIMEOUT_MS = 5000;
    uint32_t started = millis();
    while (uint32_t(millis() - started) < CAD_TIMEOUT_MS) {
      uint8_t value;
      if (!_io.digitalRead(EXIO_LORA_DIO1, value)) return RADIOLIB_ERR_SPI_CMD_FAILED;
      if (value == HIGH) return radio->getChannelScanResult();
      delay(1);
    }
    // isChannelActive() treats errors as busy and restarts reception
    return RADIOLIB_ERR_RX_TIMEOUT;
  }

  MeshnologyW10Radio(CustomSX1262& radio, mesh::MainBoard& board, MCP23017& io)
    : CustomSX1262Wrapper(radio, board), _io(io) { }
};
