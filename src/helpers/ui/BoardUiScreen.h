#pragma once

#include <MeshCore.h>
#include "BoardUiExtension.h"
#include "DisplayDriver.h"

class BoardUiScreen {
  mesh::BoardUiExtension* extension = nullptr;
  DisplayDriver* display = nullptr;
  bool selected = false;
  bool fullRefresh = true;

  void selectPage() {
    if (!selected) {
      selected = true;
      fullRefresh = true;
    }
  }

public:
  void begin(mesh::MainBoard& board, DisplayDriver& screen) {
    extension = board.getUiExtension();
    display = &screen;
  }

  bool available() const { return extension != nullptr; }

  uint8_t pollEvents() {
    if (!extension) return mesh::BoardUiExtension::EVENT_NONE;
    if (!display->isOn()) fullRefresh = true;
    return extension->pollEvents();
  }

  bool handleInput(uint8_t events, bool canOperate) {
    if (!extension || !canOperate) return false;

    const bool awake = display->isOn();
    if (events & mesh::BoardUiExtension::EVENT_SHOW_PAGE) {
      if (!awake) display->turnOn();
      selectPage();
      return true;
    }
    if ((events & mesh::BoardUiExtension::EVENT_PAGE_ACTION) && awake && selected) {
      extension->handlePageAction();
      return true;
    }
    return false;
  }

  bool handleFocusRequest(uint8_t events, bool canTakeFocus, bool handlingInput) {
    if (!extension || !(events & mesh::BoardUiExtension::EVENT_REQUEST_FOCUS) ||
        !canTakeFocus || handlingInput) return false;

    const bool awake = display->isOn();
    if (!awake) display->turnOn();
    selectPage();
    if (!awake) fullRefresh = true;
    return true;
  }

  bool togglePage() {
    if (!extension) return false;
    selected = !selected;
    fullRefresh = true;
    return true;
  }

  bool renderIfSelected(uint32_t& interval) {
    if (!extension || !selected) return false;
    interval = extension->render(*display, fullRefresh);
    fullRefresh = false;
    return true;
  }
};
