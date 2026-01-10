#pragma once

#include "DisplayDriver.h"

#define KEY_LEFT           0xB4
#define KEY_UP             0xB5
#define KEY_DOWN           0xB6
#define KEY_RIGHT          0xB7
#define KEY_SELECT           10
#define KEY_ENTER            13
#define KEY_CANCEL           27   // Esc
#define KEY_HOME           0xF0
#define KEY_NEXT           0xF1
#define KEY_PREV           0xF2
#define KEY_CONTEXT_MENU   0xF3

class UIScreen {
protected:
  UIScreen() { }
public:
  virtual int render(DisplayDriver& display) =0;   // return value is number of millis until next render
  virtual bool handleInput(char c) { return false; }
  virtual void poll() { }
};

