#pragma once

#include <helpers/ui/DisplayDriver.h>


#ifndef STATUS_BAR_SCROLL_MS
  #define STATUS_BAR_SCROLL_MS 80
#endif

#ifndef STATUS_BAR_SEPARATOR
  #define STATUS_BAR_SEPARATOR " | "
#endif

#ifndef STATUS_BAR_UPDATE_MS
  #define STATUS_BAR_UPDATE_MS 2000  // rebuild status string every 2s
#endif

class ScrollingStatusBar {
  char _status[160];
  int _text_width;
  int _scroll_x;
  int _display_width;
  unsigned long _next_scroll;
  unsigned long _next_update;
  bool _needs_redraw;

  // cached state for change detection
  char _last_name[32];
  uint16_t _last_batt_mv;
  bool _last_buzzer_quiet;
  bool _last_gps_on;
  bool _last_ble_on;

public:
  ScrollingStatusBar() : _text_width(0), _scroll_x(0), _display_width(72),
                          _next_scroll(0), _next_update(0), _needs_redraw(true),
                          _last_batt_mv(0), _last_buzzer_quiet(false),
                          _last_gps_on(false), _last_ble_on(false) {
    _status[0] = 0;
    _last_name[0] = 0;
  }

  void begin(int display_width) {
    _display_width = display_width;
    _scroll_x = 0;
    _next_scroll = 0;
    _next_update = 0;
  }

  // Call periodically to update the status string content.
  // Only rebuilds if values have changed or update interval has elapsed.
  void update(DisplayDriver& display, const char* node_name, uint16_t batt_millivolts,
              bool buzzer_quiet, bool gps_on, bool ble_on) {

    bool changed = (batt_millivolts != _last_batt_mv)
                || (buzzer_quiet != _last_buzzer_quiet)
                || (gps_on != _last_gps_on)
                || (ble_on != _last_ble_on)
                || (strcmp(node_name, _last_name) != 0);

    if (!changed) return;

    // cache current values
    strncpy(_last_name, node_name, sizeof(_last_name) - 1);
    _last_name[sizeof(_last_name) - 1] = 0;
    _last_batt_mv = batt_millivolts;
    _last_buzzer_quiet = buzzer_quiet;
    _last_gps_on = gps_on;
    _last_ble_on = ble_on;

    float volts = batt_millivolts / 1000.0f;

    snprintf(_status, sizeof(_status),
      "%s" STATUS_BAR_SEPARATOR
      "%.2fV" STATUS_BAR_SEPARATOR
      "BUZ:%s" STATUS_BAR_SEPARATOR
      "GPS:%s" STATUS_BAR_SEPARATOR
      "BLE:%s"
      " - ",  // trailing gap before the text loops
      node_name,
      volts,
      buzzer_quiet ? "OFF" : "ON",
      gps_on ? "ON" : "OFF",
      ble_on ? "ON" : "OFF"
    );

    display.setTextSize(1);
    _text_width = display.getTextWidth(_status);
    _next_update = millis() + STATUS_BAR_UPDATE_MS;
    _needs_redraw = true;
  }

  // Returns true if the status bar needs a redraw this frame.
  bool needsRedraw() {
    if (_text_width <= _display_width) return _needs_redraw;  // static, no scrolling
    return millis() >= _next_scroll;
  }

  // Render the status bar via DisplayDriver.
  // U8g2 full-buffer mode clips to display bounds automatically,
  // and the font height stays within STATUS_BAR_HEIGHT, so no
  // explicit clip window is needed.
  void render(DisplayDriver& display) {
    if (_status[0] == 0) return;

    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);

    // if (_needs_redraw) {
    //   _text_width = display.getTextWidth(_status);
    // }

    // static text: no scrolling needed
    if (_text_width <= _display_width) {
      display.setCursor(0, 0);
      display.print(_status);
      _needs_redraw = false;
      return;
    }

    int x = _scroll_x;
    do {
      display.setCursor(x, 0);
      display.print(_status);
      x += _text_width;
    } while (x < _display_width);


    // advance scroll position
    _scroll_x--;
    if (_scroll_x <= -_text_width) _scroll_x = 0;

    _next_scroll = millis() + STATUS_BAR_SCROLL_MS;
    _needs_redraw = false;
  }

};