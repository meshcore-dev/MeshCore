#pragma once

#include <stdint.h>

class MenuModel {
public:
  enum class Page : uint8_t {
    HOME,
    MESSAGE,
    CHANNELS,
    CHANNEL,
    PRIVATE_CONTACTS,
    PRIVATE_CONVERSATION,
    CONTACTS,
    CLIENTS,
    ROOMS,
    REPEATERS,
    MISC,
    CONTACT_DETAIL,
    RADIO,
    ADVERT,
    SENSORS,
    SETTINGS,
    GPS,
    BLUETOOTH,
    DATE_TIME,
    NOTIFICATIONS,
    BLUE_LED,
    GREEN_LED,
    LED_BEHAVIOUR,
    ABOUT,
    DONATE
  };

  enum class Action : uint8_t {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    SELECT,
    BACK,
    HOME
  };

  enum class LedBehaviour : uint8_t {
    OFF,
    BLINK,
    FLICKER,
    TRIPLE_BLINK,
    ON,
    COUNT
  };

  struct State {
    Page page;
    uint16_t selected;
    uint8_t context;
  };

  struct LedPattern {
    LedBehaviour behaviour;
    bool output_on;
    uint8_t transitions_remaining;
    uint32_t next_change;
  };

private:
  static const uint8_t MAX_DEPTH = 8;
  State _state;
  State _stack[MAX_DEPTH];
  uint8_t _depth;
  LedBehaviour _led[2][7];

  void push(Page page, uint8_t context = 0) {
    if (_depth < MAX_DEPTH) _stack[_depth++] = _state;
    _state.page = page;
    _state.selected = 0;
    _state.context = context;
  }

  void selectCurrent();

public:
  MenuModel() : _state{Page::HOME, 0, 0}, _depth(0) {
    for (uint8_t led = 0; led < 2; led++) {
      for (uint8_t event = 0; event < 7; event++) {
        _led[led][event] = LedBehaviour::OFF;
      }
    }
  }

  const State& state() const { return _state; }
  uint8_t depth() const { return _depth; }
  Page parentPage() const { return _depth ? _stack[_depth - 1].page : Page::HOME; }

  static uint16_t staticItemCount(Page page);
  void handle(Action action, uint16_t item_count = 0);

  LedBehaviour ledBehaviour(uint8_t led, uint8_t event) const {
    if (led >= 2 || event >= 7) return LedBehaviour::OFF;
    return _led[led][event];
  }

  void setLedBehaviour(uint8_t led, uint8_t event, LedBehaviour value) {
    if (led < 2 && event < 7 && value < LedBehaviour::COUNT) _led[led][event] = value;
  }

  static void startLedPattern(LedPattern& pattern, LedBehaviour behaviour, uint32_t now);
  static bool updateLedPattern(LedPattern& pattern, uint32_t now);
};
