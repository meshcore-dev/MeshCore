#include <Arduino.h>
#include "target.h"

#ifdef DISPLAY_CLASS
  #include <helpers/MultiSerialInterface.h>
  extern MultiSerialInterface interface_manager;

  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
  MomentaryButton back_btn(PIN_BUTTON2, 1000, true);
  UITask ui_task(&board, &interface_manager);
#endif

