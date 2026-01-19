#include "EncoderAndButton.h"

#define ENC_DEBOUNCE_US  800
#define BTN_DEBOUNCE_MS  25

// Valid quadrature transitions table
static const int8_t enc_table[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

EncoderAndButton::EncoderAndButton(
  int8_t pinA,
  int8_t pinB,
  int8_t btnPin,
  uint16_t longPressMs,
  bool pullups
) :
  _pinA(pinA),
  _pinB(pinB),
  _btnPin(btnPin),
  _longPressMs(longPressMs)
{
  _state = 0;
  _delta = 0;
  _btnState = false;
  _btnLast = false;
  _lastEncTime = 0;
  _btnDownAt = 0;
  _lastBtnChange = 0;
}

void EncoderAndButton::begin() {
  pinMode(_pinA, INPUT_PULLUP);
  pinMode(_pinB, INPUT_PULLUP);
  pinMode(_btnPin, INPUT_PULLUP);

  _state = (digitalRead(_pinA) << 1) | digitalRead(_pinB);
}

bool EncoderAndButton::buttonPressed() const {
  return !_btnState;
}

void EncoderAndButton::readEncoder() {
  unsigned long now = micros();
  if (now - _lastEncTime < ENC_DEBOUNCE_US) return;

  _lastEncTime = now;

  _state = ((_state << 2) |
            (digitalRead(_pinA) << 1) |
             digitalRead(_pinB)) & 0x0F;

  _delta += enc_table[_state];
}

int EncoderAndButton::check() {
  int event = ENC_EVENT_NONE;

  // --- Encoder ---
  readEncoder();
  if (_delta >= 4) {
    _delta = 0;
    event = ENC_EVENT_CW;
  } else if (_delta <= -4) {
    _delta = 0;
    event = ENC_EVENT_CCW;
  }

  // --- Button ---
  bool raw = digitalRead(_btnPin);
  unsigned long now = millis();

  if (raw != _btnLast && (now - _lastBtnChange) > BTN_DEBOUNCE_MS) {
    _lastBtnChange = now;
    _btnLast = raw;

    if (!raw) {
      _btnDownAt = now;
    } else {
      if (_btnDownAt &&
          (now - _btnDownAt) < _longPressMs) {
        event = ENC_EVENT_BUTTON;
      }
      _btnDownAt = 0;
    }
  }

  if (_btnDownAt &&
      (now - _btnDownAt) >= _longPressMs) {
    event = ENC_EVENT_LONG_PRESS;
    _btnDownAt = 0;
  }

  return event;
}
