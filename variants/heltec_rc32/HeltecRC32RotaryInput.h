#pragma once

#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ui/RotaryInput.h>

class HeltecRC32RotaryInput : public RotaryInput {
public:
  explicit HeltecRC32RotaryInput(RefCountedDigitalPin* periphPower = nullptr) : periph_power(periphPower) { }

  bool begin() override;
  RotaryInputEvent poll() override;
  bool isReady() const override { return ready; }

private:
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readInput(uint8_t& value);
  RotaryInputEvent handleTransition(uint8_t newState);

  uint8_t input_state = 0x03;
  uint32_t last_event_ms = 0;
  bool ready = false;
  bool initialized = false;
  bool power_claimed = false;
  bool active_low_phase = false;
  RefCountedDigitalPin* periph_power = nullptr;
};
