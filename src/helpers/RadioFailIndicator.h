#pragma once

#include <Arduino.h>

#ifdef DISPLAY_CLASS
#include <helpers/ui/DisplayDriver.h>

static inline void showRadioErrorOnDisplay(DisplayDriver* disp) {
  if (disp == NULL) return;
  disp->startFrame();
#ifdef ST7789
  disp->setTextSize(2);
#endif
  disp->drawTextCentered(disp->width() / 2, 28, "Radio Error");
  disp->endFrame();
}
#endif

#if defined(LED_PIN) && (LED_PIN >= 0) && (LED_PIN != 0xFF)
  #define _RADIO_FAIL_LED_PIN LED_PIN
#elif defined(LED_BUILTIN) && (LED_BUILTIN >= 0) && (LED_BUILTIN != 0xFF)
  #define _RADIO_FAIL_LED_PIN LED_BUILTIN
#endif

static inline void haltWithRadioFailure() {
#if defined(_RADIO_FAIL_LED_PIN) && defined(LED_STATE_ON)
  pinMode(_RADIO_FAIL_LED_PIN, OUTPUT);
  const uint16_t UNIT_MS = 300;
  const uint16_t DASH_MS = UNIT_MS * 3;
  const uint16_t LETTER_PAUSE_MS = UNIT_MS * 2;
  const uint16_t WORD_PAUSE_MS = UNIT_MS * 4;

  while (true) {
    for (uint8_t letter = 0; letter < 3; letter++) {
      uint16_t onMs = (letter == 1) ? DASH_MS : UNIT_MS;
      for (uint8_t symbol = 0; symbol < 3; symbol++) {
        digitalWrite(_RADIO_FAIL_LED_PIN, LED_STATE_ON);
        delay(onMs);
        digitalWrite(_RADIO_FAIL_LED_PIN, !LED_STATE_ON);
        delay(UNIT_MS);
      }
      delay(LETTER_PAUSE_MS);
    }
    delay(WORD_PAUSE_MS);
  }
#else
  while (1) ;
#endif
}

#ifdef _RADIO_FAIL_LED_PIN
#undef _RADIO_FAIL_LED_PIN
#endif
