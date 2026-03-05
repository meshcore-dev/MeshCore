#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>
#include <helpers/BusyTracker.h>

class UITask {
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  BusyTracker* _busy_tracker;
  char _version_info[32];

  void renderCurrScreen();
public:
  UITask(DisplayDriver& display) : _display(&display), _busy_tracker(nullptr) { _next_read = _next_refresh = 0; }
  void begin(NodePrefs* node_prefs, BusyTracker* busy, const char* build_date, const char* firmware_version);

  void loop();
};
