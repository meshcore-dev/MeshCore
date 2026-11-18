#include <Arduino.h>
#include "target.h"

#ifdef DISPLAY_CLASS
  NullDisplayDriver display;
  UITask ui_task(&board);
#endif
