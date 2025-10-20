#pragma once

#include <RadioLib.h>
#include "MeshCore.h"

#define LR1110_IRQ_HAS_PREAMBLE                     0b0000000100  //  4     4     valid LoRa header received
#define LR1110_IRQ_HEADER_VALID                     0b0000010000  //  4     4     valid LoRa header received

class CustomLR1110 : public LR1110 {
  public:
    CustomLR1110(Module *mod) : LR1110(mod) { }

    uint8_t shiftCount = 0;

    int16_t standby() override {
      // tx resets the shift, standby is called on tx completion
      // this might not actually be what resets it, but it seems to work
      // more investigation needed
      this->shiftCount = 0;
      return LR1110::standby();
    }

    size_t getPacketLength(bool update) override {
      size_t len = LR1110::getPacketLength(update);
      if (len == 0) {
        uint32_t irq = getIrqStatus();
        if (irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR) {
          MESH_DEBUG_PRINTLN("LR1110: got header err, assuming shift");
          this->shiftCount += 4; // uint8 will loop around to 0 at 256, perfect as rx buffer is 256 bytes
        } else {
          MESH_DEBUG_PRINTLN("LR1110: got zero-length packet without header err irq");
        }
      }
      return len;
    }

    int16_t readData(uint8_t *data, size_t len) override {
      // check active modem
      uint8_t modem = RADIOLIB_LR11X0_PACKET_TYPE_NONE;
      int16_t state = getPacketType(&modem);
      RADIOLIB_ASSERT(state);
      if((modem != RADIOLIB_LR11X0_PACKET_TYPE_LORA) && 
        (modem != RADIOLIB_LR11X0_PACKET_TYPE_GFSK)) {
        return(RADIOLIB_ERR_WRONG_MODEM);
      }

      // check integrity CRC
      uint32_t irq = getIrqStatus();
      int16_t crcState = RADIOLIB_ERR_NONE;
      // Report CRC mismatch when there's a payload CRC error, or a header error and no valid header (to avoid false alarm from previous packet)
      if((irq & RADIOLIB_LR11X0_IRQ_CRC_ERR) || ((irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR) && !(irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID))) {
        crcState = RADIOLIB_ERR_CRC_MISMATCH;
      }

      // get packet length
      // the offset is needed since LR11x0 seems to move the buffer base by 4 bytes on every packet
      uint8_t offset = 0;
      size_t length = LR1110::getPacketLength(true, &offset);
      if((len != 0) && (len < length)) {
        // user requested less data than we got, only return what was requested
        length = len;
      }

      // read packet data
      state = readBuffer8(data, length, (uint8_t)(offset + this->shiftCount));  // add shiftCount to offset - only change from radiolib
      RADIOLIB_ASSERT(state);

      // clear the Rx buffer
      state = clearRxBuffer();
      RADIOLIB_ASSERT(state);

      // clear interrupt flags
      state = clearIrqState(RADIOLIB_LR11X0_IRQ_ALL);

      // check if CRC failed - this is done after reading data to give user the option to keep them
      RADIOLIB_ASSERT(crcState);

      return(state);
    }

    RadioLibTime_t getTimeOnAir(size_t len) override {
  // calculate number of symbols
  float N_symbol = 0;
  if(this->codingRate <= RADIOLIB_LR11X0_LORA_CR_4_8_SHORT) {
    // legacy coding rate - nice and simple
    // get SF coefficients
    float coeff1 = 0;
    int16_t coeff2 = 0;
    int16_t coeff3 = 0;
    if(this->spreadingFactor < 7) {
      // SF5, SF6
      coeff1 = 6.25;
      coeff2 = 4*this->spreadingFactor;
      coeff3 = 4*this->spreadingFactor;
    } else if(this->spreadingFactor < 11) {
      // SF7. SF8, SF9, SF10
      coeff1 = 4.25;
      coeff2 = 4*this->spreadingFactor + 8;
      coeff3 = 4*this->spreadingFactor;
    } else {
      // SF11, SF12
      coeff1 = 4.25;
      coeff2 = 4*this->spreadingFactor + 8;
      coeff3 = 4*(this->spreadingFactor - 2);
    }

    // get CRC length
    int16_t N_bitCRC = 16;
    if(this->crcTypeLoRa == RADIOLIB_LR11X0_LORA_CRC_DISABLED) {
      N_bitCRC = 0;
    }

    // get header length
    int16_t N_symbolHeader = 20;
    if(this->headerType == RADIOLIB_LR11X0_LORA_HEADER_IMPLICIT) {
      N_symbolHeader = 0;
    }

    // calculate number of LoRa preamble symbols - NO! Lora preamble is already in symbols
    // uint32_t N_symbolPreamble = (this->preambleLengthLoRa & 0x0F) * (uint32_t(1) << ((this->preambleLengthLoRa & 0xF0) >> 4));

    // calculate the number of symbols - nope
    // N_symbol = (float)N_symbolPreamble + coeff1 + 8.0f + ceilf((float)RADIOLIB_MAX((int16_t)(8 * len + N_bitCRC - coeff2 + N_symbolHeader), (int16_t)0) / (float)coeff3) * (float)(this->codingRate + 4);
    // calculate the number of symbols - using only preamblelora because it's already in symbols
    N_symbol = (float)preambleLengthLoRa + coeff1 + 8.0f + ceilf((float)RADIOLIB_MAX((int16_t)(8 * len + N_bitCRC - coeff2 + N_symbolHeader), (int16_t)0) / (float)coeff3) * (float)(this->codingRate + 4);
  } else {
    // long interleaving - not needed for this modem
  }

  // get time-on-air in us
  return(((uint32_t(1) << this->spreadingFactor) / this->bandwidthKhz) * N_symbol * 1000.0f);
}

    bool isReceiving() {
      uint16_t irq = getIrqStatus();
      bool detected = ((irq & LR1110_IRQ_HEADER_VALID) || (irq & LR1110_IRQ_HAS_PREAMBLE));
      return detected;
    }
};