#pragma once

#include <RadioLib.h>

class CustomLR1121 : public LR1121 {
public:
  CustomLR1121(Module* mod) : LR1121(mod) { }

  int16_t setFrequency(float freq) override {
    _is_24ghz = (freq >= 2400.0f);
    return LR1121::setFrequency(freq);
  }

  int16_t setFrequency(float freq, bool skipCalibration, float band = 4) {
    _is_24ghz = (freq >= 2400.0f);
    return LR1121::setFrequency(freq, skipCalibration, band);
  }

  int16_t setOutputPower(int8_t power) override {
    return LR1121::setOutputPower(clampTxPower(power));
  }

  int16_t setOutputPower(int8_t power, bool forceHighPower, uint32_t rampTimeUs = 48) {
    return LR1121::setOutputPower(clampTxPower(power), forceHighPower, rampTimeUs);
  }

  size_t getPacketLength(bool update) override {
    size_t len = LR1121::getPacketLength(update);
    if (len == 0 && (getIrqStatus() & RADIOLIB_LR11X0_IRQ_HEADER_ERR)) {
      // Recover from sporadic shifted-packet state seen after header errors.
      standby();
    }
    return len;
  }

  bool isReceiving() {
    uint16_t irq = getIrqStatus();
    return (irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID) ||
           (irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
  }

private:
  bool _is_24ghz = false;

  int8_t clampTxPower(int8_t power) const {
    const int8_t maxDbm = _is_24ghz ? 20 : 22;
    return (power > maxDbm) ? maxDbm : power;
  }
};
