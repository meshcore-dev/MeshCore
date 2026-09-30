#pragma once

#include <Arduino.h>

// Called on the application thread after pending application saves have finished.
// A failed status read or a busy device must never lead to a power cut.
template <typename Flash>
bool shutdownX1Flash(Flash& flash) {
  const uint32_t started = millis();
  for (;;) {
    if (flash.transferIdle()) {
      uint8_t status = 0xff;
      if (!flash.readStatus(status)) return false;
      if ((status & 0x01) == 0) break;  // NOR status register WIP
    }
    if (uint32_t(millis() - started) >= 5000) return false;
    delay(1);
  }

  // All application files are closed by the synchronous DataStore save methods.
  flash.unmount();
  flash.stopBus();
  flash.releasePins();
  flash.disablePower();
  return true;
}
