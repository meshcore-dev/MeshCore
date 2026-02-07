#include "MomentaryButton.h"

#define MULTI_CLICK_WINDOW_MS  280

MomentaryButton::MomentaryButton(int8_t pin, int long_press_millis, bool reverse, bool pulldownup, bool multiclick, int debounce_ms) { 
  _pin = pin;
  _reverse = reverse;
  _pull = pulldownup;
  down_at = 0; 
  prev = _reverse ? HIGH : LOW;
  cancel = 0;
  _long_millis = long_press_millis;
  _threshold = 0;
  _click_count = 0;
  _last_click_time = 0;
  _multi_click_window = multiclick ? MULTI_CLICK_WINDOW_MS : 0;
  _pending_click = false;
  _debounce_ms = debounce_ms;
  _last_debounce_time = 0;
  _last_read = prev;
}

MomentaryButton::MomentaryButton(int8_t pin, int long_press_millis, int analog_threshold, int debounce_ms) {
  _pin = pin;
  _reverse = false;
  _pull = false;
  down_at = 0;
  prev = LOW;
  cancel = 0;
  _long_millis = long_press_millis;
  _threshold = analog_threshold;
  _click_count = 0;
  _last_click_time = 0;
  _multi_click_window = MULTI_CLICK_WINDOW_MS;
  _pending_click = false;
  _debounce_ms = debounce_ms;
  _last_debounce_time = 0;
  _last_read = prev;
}

void MomentaryButton::begin() {
  if (_pin >= 0 && _threshold == 0) {
    pinMode(_pin, _pull ? (_reverse ? INPUT_PULLUP : INPUT_PULLDOWN) : INPUT);
  }
}

bool  MomentaryButton::isPressed() const {
  int btn = _threshold > 0 ? (analogRead(_pin) < _threshold) : digitalRead(_pin);
  return isPressed(btn);
}

void MomentaryButton::cancelClick() {
  cancel = 1;
  down_at = 0;
  _click_count = 0;
  _last_click_time = 0;
  _pending_click = false;
}

bool MomentaryButton::isPressed(int level) const {
  if (_threshold > 0) {
    return level;
  }
  if (_reverse) {
    return level == LOW;
  } else {
    return level != LOW;
  }
}

void MomentaryButton::setDebounceMs(int ms) {
  if (ms < 0) ms = 0;
  _debounce_ms = ms;
}

int MomentaryButton::getDebounceMs() const {
  return _debounce_ms;
}

int MomentaryButton::check(bool repeat_click) {
  if (_pin < 0) return BUTTON_EVENT_NONE;

  int event = BUTTON_EVENT_NONE;
  int raw_btn = _threshold > 0 ? (analogRead(_pin) < _threshold) : digitalRead(_pin);

  // debounce: detect changes and wait for stability
  if (raw_btn != _last_read) {
    _last_debounce_time = millis();
    _last_read = raw_btn;
  }

  int btn = raw_btn; // use the latest raw reading for logic below

  // Only accept the state change if it has been stable for _debounce_ms
  if ((unsigned long)(millis() - _last_debounce_time) >= (unsigned long)_debounce_ms) {
    if (btn != prev) {
      if (isPressed(btn)) {
        down_at = millis();
      } else {
        // button UP
        if (_long_millis > 0) {
          if (down_at > 0 && (unsigned long)(millis() - down_at) < _long_millis) {  // only a CLICK if still within the long_press millis
              _click_count++;
              _last_click_time = millis();
              _pending_click = true;
          }
        } else {
            _click_count++;
            _last_click_time = millis();
            _pending_click = true;
        }
        if (event == BUTTON_EVENT_CLICK && cancel) {
          event = BUTTON_EVENT_NONE;
          _click_count = 0;
          _last_click_time = 0;
          _pending_click = false;
        }
        down_at = 0;
      }
      prev = btn;
    }
  }
  if (!isPressed(btn) && cancel) {   // always clear the pending 'cancel' once button is back in UP state
    cancel = 0;
  }

  if (_long_millis > 0 && down_at > 0 && (unsigned long)(millis() - down_at) >= _long_millis) {
    if (_pending_click) {
      // long press during multi-click detection - cancel pending clicks
      cancelClick();
    } else {
      event = BUTTON_EVENT_LONG_PRESS;
      down_at = 0;
      _click_count = 0;
      _last_click_time = 0;
      _pending_click = false;
    }
  }
  if (down_at > 0 && repeat_click) {
    unsigned long diff = (unsigned long)(millis() - down_at);
    if (diff >= 700) {
      event = BUTTON_EVENT_CLICK;   // wait 700 millis before repeating the click events
    }
  }

  if (_pending_click && (millis() - _last_click_time) >= _multi_click_window) {
    if (down_at > 0) {
      // still pressed - wait for button release before processing clicks
      return event;
    }
    switch (_click_count) {
      case 1:
        event = BUTTON_EVENT_CLICK;
        break;
      case 2:
        event = BUTTON_EVENT_DOUBLE_CLICK;
        break;
      case 3:
        event = BUTTON_EVENT_TRIPLE_CLICK;
        break;
      default:
        // For 4+ clicks, treat as triple click?

        event = BUTTON_EVENT_TRIPLE_CLICK;
        break;
    }
    _click_count = 0;
    _last_click_time = 0;
    _pending_click = false;
  }

  return event;
}