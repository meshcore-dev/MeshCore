#include <Arduino.h>
#include "target.h"

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);

  UITask ui_task(&board);
#endif
