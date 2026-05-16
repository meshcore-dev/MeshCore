#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>

class UITask {
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];

  unsigned long _status_until;  // millis when status screen expires
  uint16_t _status_batt_mv;
  unsigned long _status_uptime_ms;
  bool _status_charging;

  void renderCurrScreen();
public:
  UITask(DisplayDriver& display) : _display(&display) { _next_read = _next_refresh = 0; _status_until = 0; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version);
  void showStatus(uint16_t batt_mv, unsigned long uptime_ms, bool charging);

  void loop();
};