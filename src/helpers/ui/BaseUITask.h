#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
// #include <helpers/ui/UIScreen.h>

class BaseUITask {
protected:
  mesh::MainBoard* _board;
  DisplayDriver* _display;
  unsigned long _next_refresh, _auto_off;

  BaseUITask(mesh::MainBoard* board, DisplayDriver* display) : _board(board), _display(display) { _next_refresh = 0; }

  virtual long renderCurrScreen() = 0;

public:
  virtual void loop();
};
