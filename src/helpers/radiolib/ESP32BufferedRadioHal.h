#pragma once
#include <hal/Arduino/ArduinoHal.h>

#ifndef MC_SX1262_SPI_HZ
#define MC_SX1262_SPI_HZ 8000000
#endif

// Opted-in ESP32 boards only. Keep RadioLib's transaction/CS, BUSY polling and
// status parsing unchanged; send each full-duplex buffer in one SPI operation.
// SX1262 boards retain their configured default; LR1110 boards pass 16 MHz.
class ESP32BufferedRadioHal : public ArduinoHal {
 public:
  explicit ESP32BufferedRadioHal(SPIClass& bus, uint32_t hz = MC_SX1262_SPI_HZ)
      : ArduinoHal(bus, SPISettings(hz, MSBFIRST, SPI_MODE0)) {}
  void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override {
    if (len) spi->transferBytes(out, in, len);
  }
};
