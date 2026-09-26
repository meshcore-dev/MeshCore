#include "RotaryInputGPIO.h"

bool RotaryInputGPIO::begin() {

  if (_pin_a >= 0) {
    pinMode(_pin_a, _pull_a ? INPUT_PULLUP : INPUT);
  }
  if (_pin_b >= 0) {
    pinMode(_pin_b, _pull_b ? INPUT_PULLUP : INPUT);
  }

  b_prec = digitalRead(_pin_b);

  return true;
}

RotaryInputEvent RotaryInputGPIO::poll() {
  RotaryInputEvent ev = RotaryInputEvent::None;
  bool a = digitalRead(_pin_a);
  bool b = digitalRead(_pin_b);

  // this is the simplest scheme and it works well
  // for thinknode M8, just read A when B rises ;)
  if (!b_prec && b) { // rising edge of A
    if (a) {
      ev = RotaryInputEvent::Next;
    } else { 
      ev = RotaryInputEvent::Prev;
    }
  }
  b_prec = b;

  return ev;
}