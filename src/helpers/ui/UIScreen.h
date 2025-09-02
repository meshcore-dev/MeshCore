#pragma once

#include "DisplayDriver.h"

#define KEY_LEFT     0xB4
#define KEY_UP       0xB5
#define KEY_DOWN     0xB6
#define KEY_RIGHT    0xB7
#define KEY_SELECT     10
#define KEY_ENTER      13
#define KEY_BACK       27   // Esc

class UIScreen {
protected:
  UIScreen() { }
  bool _changed = true;
  bool _entered = true;
public:
  virtual int render(DisplayDriver& display) =0;   // return value is number of millis until next render
  virtual bool handleInput(char c) { return false; }
  virtual void poll() { }
  virtual void enter () { _entered = true; }
  bool has_changed () { return _changed; } 
};

