#pragma once

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include "../examples/companion_radio/NodePrefs.h"
#include "../examples/companion_radio/AbstractUITask.h"

class M5StackUnitC6LBoard;

// Minimal UITask for 64x48 display - just shows PIN and basic status
class UITask : public AbstractUITask {
public:
  UITask(M5StackUnitC6LBoard* board, BaseSerialInterface* serial)
    : AbstractUITask((mesh::MainBoard*)board, serial), _display(NULL) {}

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  void loop() override;

  void notify(UIEventType t) override {}
  void msgRead(int msgcount) override { _msgcount = msgcount; _need_refresh = true; }
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;

private:
  void renderScreen();

  DisplayDriver* _display;
  NodePrefs* _node_prefs;
  int _msgcount;
  bool _need_refresh;
  uint32_t _next_refresh;
  uint32_t _auto_off;
};
