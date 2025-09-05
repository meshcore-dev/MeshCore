#pragma once

#include "CustomSX1276.h"
#include "RadioLibWrappers.h"

class CustomSX1276Wrapper : public RadioLibWrapper {
public:
  CustomSX1276Wrapper(CustomSX1276& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { 
    return ((CustomSX1276 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1276 *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((CustomSX1276 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSX1276 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1276 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }

  // Poll RadioLib IRQ flags to simulate ISR on DIO0-less boards
  void pollIrq() override {
  #if defined(SX127X_USE_POLLING)
    // Read raw IRQ flags from SX127x
    uint16_t irq = ((SX1276 *)_radio)->getIRQFlags();
    if (irq == 0) return;

    // Bit masks per Semtech SX1276 datasheet
    constexpr uint8_t SX127X_IRQ_RX_DONE            = 0x40;
    constexpr uint8_t SX127X_IRQ_TX_DONE            = 0x08;
    constexpr uint8_t SX127X_IRQ_PAYLOAD_CRC_ERROR  = 0x20;
    constexpr uint8_t SX127X_IRQ_VALID_HEADER       = 0x10;
    constexpr uint8_t SX127X_IRQ_CAD_DONE           = 0x04;
    constexpr uint8_t SX127X_IRQ_FHSS_CHANGE_CH     = 0x02;
    constexpr uint8_t SX127X_IRQ_CAD_DETECTED       = 0x01;
    constexpr uint8_t SX127X_IRQ_RX_TIMEOUT         = 0x80;

    bool txDoneBit = (irq & SX127X_IRQ_TX_DONE) != 0;
    bool rxDoneBit = (irq & SX127X_IRQ_RX_DONE) != 0;
    bool validHeaderBit = (irq & SX127X_IRQ_VALID_HEADER) != 0;

    if (txDoneBit) {
      ((SX1276 *)_radio)->clearIrqFlags(SX127X_IRQ_TX_DONE);
      notifyISR();
      return;
    }

    // RX done: do not clear here
    if (rxDoneBit) {
      notifyISR();
      return;
    }

    // Cleanup non-terminal flags
    uint8_t cleanupMask = 0x00;
    if (irq & SX127X_IRQ_PAYLOAD_CRC_ERROR)    cleanupMask |= SX127X_IRQ_PAYLOAD_CRC_ERROR;
    if (irq & SX127X_IRQ_RX_TIMEOUT)           cleanupMask |= SX127X_IRQ_RX_TIMEOUT;
    if (irq & SX127X_IRQ_CAD_DONE)             cleanupMask |= SX127X_IRQ_CAD_DONE;
    if (irq & SX127X_IRQ_CAD_DETECTED)         cleanupMask |= SX127X_IRQ_CAD_DETECTED;
    if (irq & SX127X_IRQ_FHSS_CHANGE_CH)       cleanupMask |= SX127X_IRQ_FHSS_CHANGE_CH;
    if (cleanupMask) {
      ((SX1276 *)_radio)->clearIrqFlags(cleanupMask);
    }

    if (validHeaderBit) {
      MESH_DEBUG_PRINTLN("SX1276 poll: VALID_HEADER seen, waiting for RX_DONE...");
    }
  #endif
  }
};
