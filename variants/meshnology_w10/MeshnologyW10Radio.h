#pragma once

#include <helpers/radiolib/MCP23017Hal.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>

class MeshnologyW10Radio : public CustomSX1262Wrapper {
  static_assert(MCP23017Hal::isVirtualPin(P_LORA_DIO_1), "SX1262 DIO1 is an MCP23017 line and must be given as a virtual pin");
  static constexpr uint8_t EXIO_LORA_DIO1 = MCP23017Hal::expanderPin(P_LORA_DIO_1);

  MCP23017& _io;
  MCP23017Hal& _hal;
  bool _tx_pending = false;

public:
  void loop() override {
    // DIO1 stays high until the IRQ is cleared, so poll only during RX or a pending TX.
    // Idle polling could latch a CAD or TX completion left by a failed startReceive().
    if (isInRecvMode() || _tx_pending) _hal.poll();
    CustomSX1262Wrapper::loop();
  }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    _tx_pending = CustomSX1262Wrapper::startSendRaw(bytes, len);
    return _tx_pending;
  }

  void onSendFinished() override {
    _tx_pending = false;
    CustomSX1262Wrapper::onSendFinished();
  }

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

  MeshnologyW10Radio(CustomSX1262& radio, mesh::MainBoard& board, MCP23017& io, MCP23017Hal& hal)
    : CustomSX1262Wrapper(radio, board), _io(io), _hal(hal) { }
};
