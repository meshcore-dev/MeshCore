#pragma once

#include <WaveshareBoard.h>

// Same RP2040 board support as the Waveshare RP2040-LoRa, minus its battery
// sense divider on GPIO28, which a Dragino shield wiring doesn't have.
class PicoDraginoBoard : public WaveshareBoard {
public:
  uint16_t getBattMilliVolts() override { return 0; }   // no battery sensing
  const char *getManufacturerName() const override { return "Pico + Dragino SX1276"; }
};
