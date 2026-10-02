#pragma once

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#include <Arduino.h>
#include <helpers/SensorManager.h>
#include <helpers/ui/DisplayDriver.h>

#ifndef AUTO_OFF_MILLIS
#define AUTO_OFF_MILLIS 15000
#endif

class UITask : public AbstractUITask {
  DisplayDriver *_display;
  unsigned long _next_refresh;
  unsigned long _auto_off;
  unsigned long _started_at;
  unsigned long _footer_started_at;
  int _msgcount;

  void render();
  void wakeDisplay();

public:
  UITask(mesh::MainBoard *board, MultiSerialInterface *serial)
      : AbstractUITask(board, serial), _display(NULL), _next_refresh(0), _auto_off(0), _started_at(0),
        _footer_started_at(0), _msgcount(0) {}

  void begin(DisplayDriver *display, SensorManager *sensors, NodePrefs *node_prefs);

  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char *from_name, const char *text, int msgcount) override;
  void notify(UIEventType type = UIEventType::none) override;
  void loop() override;
};
