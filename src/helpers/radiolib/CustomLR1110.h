#pragma once

#include <RadioLib.h>
#include "MeshCore.h"

class CustomLR1110 : public LR1110 {
  bool _rx_boosted = false;

  public:
    CustomLR1110(Module *mod) : LR1110(mod) { }

    size_t getPacketLength(bool update) override {
      size_t len = LR1110::getPacketLength(update);
      if (len == 0 && getIrqStatus() & RADIOLIB_LR11X0_IRQ_HEADER_ERR) {
        // we've just received a corrupted packet
        // this may have triggered a bug causing subsequent packets to be shifted
        // call standby() to return radio to known-good state
        // recvRaw will call startReceive() to restart rx
        MESH_DEBUG_PRINTLN("LR1110: got header err, calling standby()");
        standby();
      }
      return len;
    }
    
    float getFreqMHz() const { return freqMHz; }

    int16_t startReceiveDutyCycle(uint32_t rxPeriod, uint32_t sleepPeriod,
                                  RadioLibIrqFlags_t irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS,
                                  RadioLibIrqFlags_t irqMask = RADIOLIB_IRQ_RX_DEFAULT_MASK) {
      // RadioLib's LR11x0 duty-cycle path stages RX but does not call
      // launchMode(), where the software RF switch is normally set to RX.
      uint32_t transitionTime = this->tcxoDelay + 1000;
      sleepPeriod -= transitionTime;

      uint32_t rxPeriodRaw = (rxPeriod * 32768UL) / 1000000UL;
      uint32_t sleepPeriodRaw = (sleepPeriod * 32768UL) / 1000000UL;

      if ((rxPeriodRaw & 0xFF000000) || (rxPeriodRaw == 0)) {
        return RADIOLIB_ERR_INVALID_RX_PERIOD;
      }

      if ((sleepPeriodRaw & 0xFF000000) || (sleepPeriodRaw == 0)) {
        return RADIOLIB_ERR_INVALID_SLEEP_PERIOD;
      }

      RadioModeConfig_t cfg = {
        .receive = {
          .timeout = RADIOLIB_LR11X0_RX_TIMEOUT_INF,
          .irqFlags = irqFlags,
          .irqMask = irqMask,
          .len = 0,
        }
      };
      int16_t state = this->stageMode(RADIOLIB_RADIO_MODE_RX, &cfg);
      RADIOLIB_ASSERT(state);

      this->mod->setRfSwitchState(Module::MODE_RX);
      return this->setRxDutyCycle(rxPeriodRaw, sleepPeriodRaw, RADIOLIB_LR11X0_RX_DUTY_CYCLE_MODE_RX);
    }

    int16_t setRxBoostedGainMode(bool en) {
      _rx_boosted = en;
      return LR1110::setRxBoostedGainMode(en);
    }

    bool getRxBoostedGainMode() const { return _rx_boosted; }

    bool isReceiving() {
      // BUSY high means the chip is asleep (RX duty-cycle sleep window) or mid
      // command, so it cannot be mid-receive - and the SPI read below would
      // stall until the chip's next listen window.
      uint32_t busy = this->mod->getGpio();
      if (busy != RADIOLIB_NC && this->mod->hal->digitalRead(busy)) return false;

      uint16_t irq = getIrqStatus();
      bool detected = ((irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID) || (irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED));
      return detected;
    }

    uint8_t getSpreadingFactor() const { return spreadingFactor; }
};
