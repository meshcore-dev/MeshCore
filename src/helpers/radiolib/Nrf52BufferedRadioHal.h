#pragma once

#include <hal/Arduino/ArduinoHal.h>

// nRF52840 SPIClass::transfer(uint8_t) starts one SPIM transaction per byte.
// RadioLib sends whole command buffers; keep its CS, BUSY, and status handling
// but transfer each buffer in one SPIM transaction. LR1110 supports 16 MHz SPI.
class Nrf52BufferedRadioHal : public ArduinoHal {
public:
  explicit Nrf52BufferedRadioHal(SPIClass& bus, uint32_t hz = 16000000)
      : ArduinoHal(bus, SPISettings(hz, MSBFIRST, SPI_MODE0)) {}

  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
    if (len) spi->transfer(out, in, len);
  }
};
