#include "BaseUITask.h"
#include <Arduino.h>

void BaseUITask::loop() {
  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      long interval = renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + interval;
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}