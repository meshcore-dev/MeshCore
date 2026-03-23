#pragma once

#include <RadioLib.h>

#define SX126X_HELPER_IRQ_HEADER_VALID 0b0000010000

template <typename RadioT>
inline int sx126xClearAllIrqs(RadioT& radio) {
  return radio.clearIrqStatus();
}

template <typename RadioT>
inline int sx126xTryReadData(RadioT& radio, uint8_t* data, size_t max_len, size_t* out_len = nullptr) {
  int16_t state = radio.mod->SPIcheckStream();
  uint16_t irq = radio.getIrqFlags();
  if ((state == RADIOLIB_ERR_SPI_CMD_TIMEOUT) && (irq & RADIOLIB_SX126X_IRQ_TIMEOUT)) {
    return RADIOLIB_ERR_RX_TIMEOUT;
  }
  if (state != RADIOLIB_ERR_NONE) {
    return state;
  }

  int16_t crcState = RADIOLIB_ERR_NONE;
  if ((irq & RADIOLIB_SX126X_IRQ_CRC_ERR) || ((irq & RADIOLIB_SX126X_IRQ_HEADER_ERR) && !(irq & SX126X_HELPER_IRQ_HEADER_VALID))) {
    crcState = RADIOLIB_ERR_CRC_MISMATCH;
  }

  uint8_t offset = 0;
  size_t length = radio.implicitLen;
  if (radio.headerType != RADIOLIB_SX126X_LORA_HEADER_IMPLICIT) {
    uint8_t rxBufStatus[2] = {0, 0};
    state = radio.mod->SPIreadStream(RADIOLIB_SX126X_CMD_GET_RX_BUFFER_STATUS, rxBufStatus, 2);
    if (state != RADIOLIB_ERR_NONE) {
      return state;
    }

    offset = rxBufStatus[1];
    length = (size_t)rxBufStatus[0];
  }

  if ((max_len != 0) && (max_len < length)) {
    length = max_len;
  }

  state = radio.readBuffer(data, length, offset);
  if (state != RADIOLIB_ERR_NONE) {
    return state;
  }

  state = sx126xClearAllIrqs(radio);
  if (crcState != RADIOLIB_ERR_NONE) {
    return crcState;
  }
  if (state != RADIOLIB_ERR_NONE) {
    return state;
  }

  if (out_len) {
    *out_len = length;
  }
  return RADIOLIB_ERR_NONE;
}
