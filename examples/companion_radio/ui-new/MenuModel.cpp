#include "MenuModel.h"

static uint16_t ledPatternInterval(MenuModel::LedBehaviour behaviour) {
  switch (behaviour) {
    case MenuModel::LedBehaviour::BLINK:        return 300;
    case MenuModel::LedBehaviour::FLICKER:      return 60;
    case MenuModel::LedBehaviour::TRIPLE_BLINK: return 150;
    default:                                    return 0;
  }
}

void MenuModel::startLedPattern(LedPattern& pattern, LedBehaviour behaviour, uint32_t now) {
  pattern.behaviour = behaviour;
  pattern.transitions_remaining = 0;
  pattern.next_change = 0;
  pattern.output_on = behaviour != LedBehaviour::OFF;

  if (behaviour == LedBehaviour::BLINK) {
    pattern.transitions_remaining = 1;
  } else if (behaviour == LedBehaviour::FLICKER ||
             behaviour == LedBehaviour::TRIPLE_BLINK) {
    pattern.transitions_remaining = 5;
  }
  if (pattern.transitions_remaining) {
    pattern.next_change = now + ledPatternInterval(behaviour);
  }
}

bool MenuModel::updateLedPattern(LedPattern& pattern, uint32_t now) {
  if (pattern.transitions_remaining == 0 ||
      static_cast<int32_t>(now - pattern.next_change) < 0) {
    return false;
  }
  pattern.output_on = !pattern.output_on;
  pattern.transitions_remaining--;
  if (pattern.transitions_remaining) {
    pattern.next_change += ledPatternInterval(pattern.behaviour);
  }
  return true;
}

uint16_t MenuModel::staticItemCount(Page page) {
  switch (page) {
    case Page::HOME:          return 6;
    case Page::MESSAGE:       return 2;
    case Page::CONTACTS:      return 4;
    case Page::SETTINGS:      return 6;
    case Page::NOTIFICATIONS: return 2;
    case Page::BLUE_LED:
    case Page::GREEN_LED:     return 7;
    case Page::LED_BEHAVIOUR: return static_cast<uint8_t>(LedBehaviour::COUNT);
    default:                  return 0;
  }
}

void MenuModel::selectCurrent() {
  switch (_state.page) {
    case Page::HOME:
      switch (_state.selected) {
        case 0: push(Page::MESSAGE); break;
        case 1: push(Page::CONTACTS); break;
        case 2: push(Page::RADIO); break;
        case 3: push(Page::ADVERT); break;
        case 4: push(Page::SENSORS); break;
        case 5: push(Page::SETTINGS); break;
      }
      break;
    case Page::MESSAGE:
      push(_state.selected == 0 ? Page::CHANNELS : Page::PRIVATE_CONTACTS);
      break;
    case Page::CHANNELS:
      push(Page::CHANNEL, static_cast<uint8_t>(_state.selected));
      break;
    case Page::PRIVATE_CONTACTS:
      push(Page::PRIVATE_CONVERSATION, static_cast<uint8_t>(_state.selected));
      break;
    case Page::CONTACTS:
      switch (_state.selected) {
        case 0: push(Page::CLIENTS); break;
        case 1: push(Page::ROOMS); break;
        case 2: push(Page::REPEATERS); break;
        case 3: push(Page::MISC); break;
      }
      break;
    case Page::CLIENTS:
    case Page::ROOMS:
    case Page::REPEATERS:
    case Page::MISC:
      push(Page::CONTACT_DETAIL, static_cast<uint8_t>(_state.selected));
      break;
    case Page::SETTINGS:
      switch (_state.selected) {
        case 0: push(Page::GPS); break;
        case 1: push(Page::BLUETOOTH); break;
        case 2: push(Page::DATE_TIME); break;
        case 3: push(Page::NOTIFICATIONS); break;
        case 4: push(Page::ABOUT); break;
        case 5: push(Page::DONATE); break;
      }
      break;
    case Page::NOTIFICATIONS:
      push(_state.selected == 0 ? Page::BLUE_LED : Page::GREEN_LED);
      break;
    case Page::BLUE_LED:
    case Page::GREEN_LED:
      push(Page::LED_BEHAVIOUR, static_cast<uint8_t>(_state.selected));
      break;
    case Page::LED_BEHAVIOUR: {
      uint8_t led = _depth > 0 && _stack[_depth - 1].page == Page::GREEN_LED ? 1 : 0;
      setLedBehaviour(led, _state.context, static_cast<LedBehaviour>(_state.selected));
      if (_depth > 0) _state = _stack[--_depth];
      break;
    }
    default:
      break;
  }
}

void MenuModel::handle(Action action, uint16_t item_count) {
  if (action == Action::HOME) {
    _state = {Page::HOME, 0, 0};
    _depth = 0;
    return;
  }
  if (action == Action::BACK) {
    if (_depth > 0) _state = _stack[--_depth];
    return;
  }

  uint16_t count = item_count ? item_count : staticItemCount(_state.page);
  if (count == 0) return;

  if (_state.page == Page::HOME) {
    if (action == Action::UP || action == Action::LEFT) {
      _state.selected = _state.selected == 0 ? count - 1 : _state.selected - 1;
    }
    if (action == Action::DOWN || action == Action::RIGHT) {
      _state.selected = (_state.selected + 1) % count;
    }
  } else {
    if (action == Action::UP) {
      _state.selected = _state.selected == 0 ? count - 1 : _state.selected - 1;
    }
    if (action == Action::DOWN) {
      _state.selected = (_state.selected + 1) % count;
    }
  }

  if (action == Action::SELECT) selectCurrent();
}
