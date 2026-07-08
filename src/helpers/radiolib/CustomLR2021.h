#pragma once

#include <RadioLib.h>
#include "MeshCore.h"

// Custom RadioLib driver for the Semtech LR2021 (LoRa Plus), as used on the
// Seeed Wio-LR2021 (LR2021 + nRF54L15). Two board/chip quirks are handled here
// (both discovered during hardware bring-up):


#ifndef LR2021_IRQ_DIO
  #define LR2021_IRQ_DIO 8        // Wio-LR2021 wires the LR2021's DIO8 to the host IRQ line
#endif

#ifndef LR2021_RX_BOOST_LEVEL
  #define LR2021_RX_BOOST_LEVEL 7 // matches Semtech usp_zephyr rx-boost-cfg = <7>
#endif

class CustomLR2021 : public LR2021 {
  uint8_t _rx_boost_level = 0;

public:
  CustomLR2021(Module *mod) : LR2021(mod) { }

  // route the host IRQ to the DIO the board actually wires (std_init does this
  // too; callers that do their own begin() sequence use this directly)
  void setIrqDio(uint8_t n) { irqDioNum = n; }

#if defined(ARDUINO)   // Arduino-core bring-up; Zephyr (compat shim, no SPIClass) drives begin() itself
  bool std_init(SPIClass* spi = NULL) {
    // route the host IRQ to the DIO the board actually wires (default DIO8)
    irqDioNum = LR2021_IRQ_DIO;

  #ifdef LORA_CR
    uint8_t cr = LORA_CR;
  #else
    uint8_t cr = 5;
  #endif

  #if defined(P_LORA_SCLK)
    #if defined(ESP32_PLATFORM)
      if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    #elif defined(NRF52_PLATFORM)
      if (spi) { spi->setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI); spi->begin(); }
    #else
      if (spi) spi->begin();   // bare-metal nRF54L15 core: SPI pins are fixed (D8/D9/D10)
    #endif
  #else
    if (spi) spi->begin();
  #endif

    // tcxoVoltage = 0 -> skip SetTcxoMode (RadioLib mis-scales its start_time; see note above).
    int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr,
                       RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, 0.0f);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print("ERROR: LR2021 init failed: ");
      Serial.println(status);
      return false;
    }

    setCRC(2);
    explicitHeader();

  #ifdef RX_BOOSTED_GAIN
    if (RX_BOOSTED_GAIN) setRxBoostedGainMode(LR2021_RX_BOOST_LEVEL);
  #endif

    return true;  // success
  }
#endif  // ARDUINO

  size_t getPacketLength(bool update) override {
    size_t len = LR2021::getPacketLength(update);
    if (len == 0 && (getIrqFlags() & RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR)) {
      // corrupted header: return to a known-good state; recvRaw restarts RX
      MESH_DEBUG_PRINTLN("LR2021: got header CRC err, calling standby()");
      standby();
    }
    return len;
  }

#if RADIOLIB_GODMODE
  int16_t startReceive() override {
    // re-assert max payload length before every RX: a TX leaves the chip's
    // packet-length param at the last TX size, which would clip longer
    // incoming packets. Needs GODMODE (setLoRaPacketParams is private).
    setLoRaPacketParams(this->preambleLengthLoRa, this->headerType,
                        RADIOLIB_LR2021_MAX_PACKET_LENGTH, this->crcTypeLoRa,
                        this->invertIQEnabled);
    return LR2021::startReceive();
  }
#endif

  bool isReceiving() {
    uint32_t irq = getIrqFlags();
    return (irq & RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED)
        || (irq & RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID);
  }

  int16_t setRxBoostedGainMode(uint8_t level) {
    _rx_boost_level = level;
    return LR2021::setRxBoostedGainMode(level);
  }

  uint8_t getRxBoostLevel() const { return _rx_boost_level; }
};
