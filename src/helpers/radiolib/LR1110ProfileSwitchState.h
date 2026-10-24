#pragma once
#include <stdint.h>

// A short-lived, acknowledged continuous-RX context. It is never valid after
// sleep, TX/CAD staging, an unrelated standby, reset, or a failed command.
struct LR1110ProfileSwitchState {
  bool enabled;
  bool rxValid = false;
  bool open = false;
  bool active = false;
  bool standbyReady = false;
  int16_t error = 0;
  bool modulationValid = false;
  uint8_t modulationSf = 0, modulationBw = 0, modulationCr = 0, modulationLdro = 0;

  explicit LR1110ProfileSwitchState(bool enable) : enabled(enable) {}
  void invalidate() {
    rxValid = active = standbyReady = modulationValid = false;
  }
  void begin(bool continuousRx, bool warm) {
    open = true;
    error = 0;
    standbyReady = false;
    active = enabled && rxValid && continuousRx && warm;
  }
  void standbyResult(int16_t result) {
    if (result != 0) {
      if (open) error = result;
      invalidate();
    } else if (active) {
      standbyReady = true;
    }
  }
  bool canResumeFast() const {
    return open && active && rxValid && standbyReady && error == 0;
  }
  bool matchesModulation(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro) const {
    return canResumeFast() && modulationValid && modulationSf == sf
        && modulationBw == bw && modulationCr == cr && modulationLdro == ldro;
  }
  void modulationResult(int16_t result, uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro) {
    if (result != 0) {
      if (open) error = result;
      invalidate();
    } else {
      modulationSf = sf;
      modulationBw = bw;
      modulationCr = cr;
      modulationLdro = ldro;
      modulationValid = true;
    }
  }
  bool failed() const { return open && error != 0; }
  void rxResult(int16_t result, bool reusable) {
    if (result != 0) {
      if (open) error = result;
      invalidate();
    } else {
      rxValid = enabled && reusable;
      standbyReady = false;
    }
  }
  void end(bool success) {
    if (!success) invalidate();
    open = active = standbyReady = false;
    error = 0;
  }
};
