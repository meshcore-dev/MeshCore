#include "HeltecRC32RotaryInput.h"

#include <Wire.h>

namespace {
constexpr uint8_t TCA6408_ADDR = 0x20;
constexpr uint8_t TCA6408_INPUT_REG = 0x00;
constexpr uint8_t TCA6408_POLARITY_REG = 0x02;
constexpr uint8_t TCA6408_CONFIG_REG = 0x03;
constexpr uint8_t TCA6408_ROTARY_A_MASK = 0x01;
constexpr uint8_t TCA6408_ROTARY_B_MASK = 0x02;
constexpr uint8_t TCA6408_ROTARY_MASK = TCA6408_ROTARY_A_MASK | TCA6408_ROTARY_B_MASK;
constexpr uint32_t TCA6408_DEBOUNCE_MS = 5;
}

bool HeltecRC32RotaryInput::begin() {
  initialized = true;
  ready = false;
  input_state = TCA6408_ROTARY_MASK;
  active_low_phase = false;

  if (periph_power && !power_claimed) {
    periph_power->claim();
    power_claimed = true;
    delay(12);
  }

  if (!writeRegister(TCA6408_POLARITY_REG, 0x00) || !writeRegister(TCA6408_CONFIG_REG, 0xFF)) {
    return false;
  }

  uint8_t state = 0;
  if (!readInput(state)) {
    return false;
  }

  input_state = state & TCA6408_ROTARY_MASK;
  ready = true;
  return true;
}

RotaryInputEvent HeltecRC32RotaryInput::poll() {
  if (!initialized) {
    begin();
    return RotaryInputEvent::None;
  }

  if (!ready) {
    return RotaryInputEvent::None;
  }

  uint8_t new_state = 0;
  if (!readInput(new_state)) {
    return RotaryInputEvent::None;
  }

  new_state &= TCA6408_ROTARY_MASK;
  RotaryInputEvent event = handleTransition(new_state);
  input_state = new_state;
  return event;
}

bool HeltecRC32RotaryInput::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool HeltecRC32RotaryInput::readInput(uint8_t& value) {
  Wire.beginTransmission(TCA6408_ADDR);
  Wire.write(TCA6408_INPUT_REG);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(TCA6408_ADDR, static_cast<uint8_t>(1)) != 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

RotaryInputEvent HeltecRC32RotaryInput::handleTransition(uint8_t newState) {
  uint8_t changed = (input_state ^ newState) & TCA6408_ROTARY_MASK;
  RotaryInputEvent event = RotaryInputEvent::None;
  bool a_low = (newState & TCA6408_ROTARY_A_MASK) == 0;
  bool b_low = (newState & TCA6408_ROTARY_B_MASK) == 0;

  if (!a_low && !b_low) {
    active_low_phase = false;
  }

  if (!active_low_phase && (changed & TCA6408_ROTARY_A_MASK) && a_low && !b_low) {
    event = RotaryInputEvent::Prev;
    active_low_phase = true;
  } else if (!active_low_phase && (changed & TCA6408_ROTARY_B_MASK) && b_low && !a_low) {
    event = RotaryInputEvent::Next;
    active_low_phase = true;
  }

  if (event == RotaryInputEvent::None && !active_low_phase && (changed & TCA6408_ROTARY_A_MASK)) {
    bool a_rising = (newState & TCA6408_ROTARY_A_MASK) != 0;
    if (a_rising && b_low) {
      event = RotaryInputEvent::Prev;
    }
  }

  if (event == RotaryInputEvent::None && !active_low_phase && (changed & TCA6408_ROTARY_B_MASK)) {
    bool b_rising = (newState & TCA6408_ROTARY_B_MASK) != 0;
    if (b_rising && a_low) {
      event = RotaryInputEvent::Next;
    }
  }

  if (event == RotaryInputEvent::None || (millis() - last_event_ms) < TCA6408_DEBOUNCE_MS) {
    return RotaryInputEvent::None;
  }

  last_event_ms = millis();
  return event;
}
