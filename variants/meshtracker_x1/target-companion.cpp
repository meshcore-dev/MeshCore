#include <Arduino.h>
#include "target.h"

#ifdef DISPLAY_CLASS
  #include <helpers/MultiSerialInterface.h>
  extern MultiSerialInterface interface_manager;

  NullDisplayDriver display;
  UITask ui_task(&board, &interface_manager);
#endif
