#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>

// Helper for millis() rollover-safe comparisons
static inline bool timePassed(unsigned long t) {
  return (int32_t)(millis() - t) >= 0;
}

class UITask {
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off;
  unsigned long _press_started;  // For button long-press detection
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  
  // Cached UI strings and values
  char _line_freq[32];
  char _line_bw[32];
  float _last_freq;
  float _last_bw;
  uint8_t _last_sf;
  uint8_t _last_cr;
  uint16_t _version_width;  // Cached text width

  void renderCurrScreen();
  void updateCachedStrings();
  
public:
  UITask(DisplayDriver& display) : _display(&display) { 
    _next_read = _next_refresh = 0; 
    _press_started = 0;
    _last_freq = 0;
    _last_bw = 0;
    _last_sf = 0;
    _last_cr = 0;
    _version_width = 0;
  }
  
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version);
  void loop();
};
