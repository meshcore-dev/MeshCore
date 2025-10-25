#pragma once

#include <RadioLib.h>

#define LR1121_IRQ_HAS_PREAMBLE                     0b0000000100  //  4     4     valid LoRa header received
#define LR1121_IRQ_HEADER_VALID                     0b0000010000  //  4     4     valid LoRa header received

class CustomLR1121 : public LR1121 {
  public:
    CustomLR1121(Module *mod) : LR1121(mod) { }

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
      bool detected = ((irq & LR1121_IRQ_HEADER_VALID) || (irq & LR1121_IRQ_HAS_PREAMBLE));
      return detected;
    }
};