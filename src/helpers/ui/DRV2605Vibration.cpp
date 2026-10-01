#ifdef HAS_DRV2605

#include "DRV2605Vibration.h"
#include <Wire.h>

void DRV2605Vibration::begin() {
#ifdef PIN_DRV_EN
  pinMode(PIN_DRV_EN, OUTPUT);
  digitalWrite(PIN_DRV_EN, HIGH);   // power up the haptic driver
  delay(10);
#endif
  if (!drv.begin(&Wire)) {
    return;   // no haptic driver found, stay silent
  }
#ifdef DRV2605_USE_LRA
  // LRA mode: 4x brake factor, medium loop gain, back-EMF gain 2
  drv.writeRegister8(DRV2605_REG_FEEDBACK, 0xB6);
#endif
  drv.selectLibrary(1);
  drv.setMode(DRV2605_MODE_INTTRIG);
  _ready = true;
}

void DRV2605Vibration::trigger(bool force) {
  if (!_ready || _quiet) return;
  unsigned long now = millis();
  if (!force && _last_trigger != 0 && now - _last_trigger < VIBRATION_TIMEOUT) return;
  _last_trigger = now;
  drv.setWaveform(0, DRV2605_EFFECT);
  drv.setWaveform(1, 0);   // pause
  drv.setWaveform(2, DRV2605_EFFECT);
  drv.setWaveform(3, 0);   // end of sequence
  drv.go();
}

void DRV2605Vibration::loop() {
}

bool DRV2605Vibration::isVibrating() {
  return false;   // effects are short, treat as instantaneous
}

void DRV2605Vibration::stop() {
  if (_ready) drv.stop();
}

#endif // ifdef HAS_DRV2605
