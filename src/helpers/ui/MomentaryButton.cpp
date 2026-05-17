#include "MomentaryButton.h"

#if defined(M5STACK_CARDPUTER) && defined(CARDPUTER_KEYBOARD_UI_NAV)
  #include <M5Cardputer.h>
#endif

#define MULTI_CLICK_WINDOW_MS  280

#if defined(M5STACK_CARDPUTER) && defined(CARDPUTER_KEYBOARD_UI_NAV)
static bool cardputerWordHas(const Keyboard_Class::KeysState& keys, char a, char b = 0, char c = 0, char d = 0) {
  for (char ch : keys.word) {
    if (ch == a || (b != 0 && ch == b) || (c != 0 && ch == c) || (d != 0 && ch == d)) {
      return true;
    }
  }
  return false;
}

static int cardputerKeyboardNavEvent() {
  static bool was_pressed = false;
  static uint32_t last_event_ms = 0;

  M5Cardputer.update();
  M5Cardputer.Keyboard.updateKeyList();
  M5Cardputer.Keyboard.updateKeysState();

  auto& keys = M5Cardputer.Keyboard.keysState();

  // Cardputer uses the punctuation keys with arrow legends for navigation:
  //   left:  ',' / '<'
  //   right: '/' / '?'  ('.' / '>' is also accepted on layouts that mark it as right)
  //   enter: physical Enter
  bool left = cardputerWordHas(keys, ',', '<');
  bool right = cardputerWordHas(keys, '/', '?', '.', '>');
  bool enter = keys.enter;
  bool any = left || right || enter;

  if (!any) {
    was_pressed = false;
    return BUTTON_EVENT_NONE;
  }

  if (was_pressed) {
    return BUTTON_EVENT_NONE;
  }

  uint32_t now = millis();
  if ((uint32_t)(now - last_event_ms) < 120UL) {
    return BUTTON_EVENT_NONE;
  }

  was_pressed = true;
  last_event_ms = now;

  if (enter) return BUTTON_EVENT_LONG_PRESS;   // existing UI maps long press to KEY_ENTER
  if (left) return BUTTON_EVENT_DOUBLE_CLICK;  // existing UI maps double click to KEY_PREV
  if (right) return BUTTON_EVENT_CLICK;        // existing UI maps click to KEY_NEXT
  return BUTTON_EVENT_NONE;
}
#endif

MomentaryButton::MomentaryButton(int8_t pin, int long_press_millis, bool reverse, bool pulldownup, bool multiclick) { 
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
}

MomentaryButton::MomentaryButton(int8_t pin, int long_press_millis, int analog_threshold) {
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

int MomentaryButton::check(bool repeat_click) {
  if (_pin < 0) return BUTTON_EVENT_NONE;

#if defined(M5STACK_CARDPUTER) && defined(CARDPUTER_KEYBOARD_UI_NAV) && defined(PIN_USER_BTN)
  if (_pin == PIN_USER_BTN && _threshold == 0) {
    int keyboard_event = cardputerKeyboardNavEvent();
    if (keyboard_event != BUTTON_EVENT_NONE) return keyboard_event;
  }
#endif

  int event = BUTTON_EVENT_NONE;
  int btn = _threshold > 0 ? (analogRead(_pin) < _threshold) : digitalRead(_pin);
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