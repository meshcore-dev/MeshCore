#include "StatusScreen.h"
#include <helpers/asr650x/OledText.h>
#include <Arduino.h>

void StatusScreen::begin(OledPanel* oled) {
  _oled = oled;
  pinMode(USER_KEY, INPUT);                                       // external 10k pull-up; pressed = LOW
  wake(millis());                                                 // visible at boot
}

void StatusScreen::wake(uint32_t now_ms) {
  if (!_oled || !_oled->present()) return;
  _oled->power(true);
  _on = _oled->is_on();
  _on_since = now_ms;
  _next_page = _on ? 0 : -1;
  _last_refresh = now_ms;
}

void StatusScreen::loop(uint32_t now_ms) {
  if (!_oled || !_oled->present()) return;
  if (_button.update(digitalRead(USER_KEY) == LOW, now_ms)) wake(now_ms);

  if (_on && now_ms - _on_since >= ON_MS) {                       // auto-off
    _oled->power(false);
    _on = false;
    _next_page = -1;
    return;
  }
  if (!_on) return;

  if (_next_page < 0 && now_ms - _last_refresh >= 1000) {         // start a refresh: battery at most every 10 s
    if (now_ms - _last_batt_ms >= 10000 || _batt_mv == 0) {
      _batt_mv = getBatteryVoltage();                             // drives the shared USER_KEY pin LOW for a moment
      _last_batt_ms = now_ms;
      _button.holdoff(millis(), 50);
    }
    _in.batt_mv = _batt_mv;
    _next_page = 0;
    _last_refresh = now_ms;
  }
  if (_next_page >= 0) {                                          // one page per call
    char line[22];
    asr650x::status_line(_next_page, _in, line);
    asr650x::render_line(line, _page);
    if (!_oled->write_page((uint8_t)_next_page, _page)) { _error = true; _on = false; _oled->power(false); _next_page = -1; return; }
    _next_page = (_next_page == 7) ? -1 : (int8_t)(_next_page + 1);
  }
}
