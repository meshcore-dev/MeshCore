#pragma once
#include <Arduino.h>

#define ENC_EVENT_NONE        0
#define ENC_EVENT_CW          1
#define ENC_EVENT_CCW         2
#define ENC_EVENT_BUTTON      3
#define ENC_EVENT_LONG_PRESS  4

class EncoderAndButton {
public:
  EncoderAndButton(
    int8_t pinA,
    int8_t pinB,
    int8_t btnPin,
    uint16_t longPressMs = 1000,
    bool pullups = true
  );

  void begin();
  int  check();                 // returns ENC_EVENT_*
  bool buttonPressed() const;

private:
  // encoder
  int8_t _pinA, _pinB;
  uint8_t _state;
  int8_t _delta;
  unsigned long _lastEncTime;

  // button
  int8_t _btnPin;
  bool _btnState;
  bool _btnLast;
  unsigned long _btnDownAt;
  uint16_t _longPressMs;
  unsigned long _lastBtnChange;

  void readEncoder();
};
